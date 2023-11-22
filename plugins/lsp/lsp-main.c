/*
 * Copyright 2023 Jiri Techet <techet@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "lsp-server.h"
#include "lsp-sync.h"
#include "lsp-utils.h"
#include "lsp-autocomplete.h"
#include "lsp-diagnostics.h"
#include "lsp-hover.h"
#include "lsp-semtokens.h"
#include "lsp-signature.h"
#include "lsp-goto.h"
#include "lsp-symbols.h"
#include "lsp-goto-anywhere.h"
#include "lsp-format.h"
#include "lsp-highlight.h"
#include "lsp-rename.h"

#include <sys/time.h>
#include <string.h>

#include <geanyplugin.h>

#include <jsonrpc-glib.h>


// https://github.com/microsoft/language-server-protocol/blob/main/versions/protocol-1-x.md
// https://github.com/microsoft/language-server-protocol/blob/main/versions/protocol-2-x.md


GeanyPlugin *geany_plugin;
GeanyData *geany_data;

LspProjectConfigurationType project_configuration_type;
gchar *project_configuration_file;

static gint last_click_pos;
static gboolean ignore_selection_change;


PLUGIN_VERSION_CHECK(246)  //TODO
PLUGIN_SET_TRANSLATABLE_INFO(
	GEANY_LOCALEDIR,
	GETTEXT_PACKAGE,
	_("LSP Client"),
	_("Language server protocol client for Geany"),
	"0.1",  //TODO: VERSION when part o geany-plugins
	"Jiri Techet <techet@gmail.com>")


enum {
  KB_GOTO_DEFINITION,
  KB_GOTO_DECLARATION,
  KB_GOTO_TYPE_DEFINITION,

  KB_GOTO_ANYWHERE,
  KB_GOTO_DOC_SYMBOL,
  KB_GOTO_WORKSPACE_SYMBOL,
  KB_GOTO_LINE,

  KB_FIND_IMPLEMENTATIONS,
  KB_FIND_REFERENCES,

  KB_SHOW_HOVER_POPUP,

  KB_RENAME_IN_FILE,
  KB_RENAME_IN_PROJECT,
  KB_FORMAT_CODE,

  KB_RESTART_SERVERS,

  KB_COUNT
};


struct
{
	GtkWidget *parent_item;
	GtkWidget *project_config;
	GtkWidget *user_config;
	// context menu
	GtkWidget *goto_type_def;
	GtkWidget *goto_ref;
	GtkWidget *rename_in_file;
	GtkWidget *rename_in_project;
	GtkWidget *format_code;
	GtkWidget *separator1;
	GtkWidget *separator2;
} menu_items;


struct
{
	GtkWidget *settings_type_combo;
	GtkWidget *config_file_entry;
	GtkWidget *path_box;
	GtkWidget *properties_tab;
} project_dialog;


static void on_document_new(G_GNUC_UNUSED GObject *obj, GeanyDocument *doc,
	G_GNUC_UNUSED gpointer user_data)
{
	// we don't know the filename yet - nothing for the LSP server
}


static void on_document_open(G_GNUC_UNUSED GObject *obj, G_GNUC_UNUSED GeanyDocument *doc,
	G_GNUC_UNUSED gpointer user_data)
{
	// we use "lazy" document opening notifications, nothing here
}


static void on_document_close(G_GNUC_UNUSED GObject * obj, GeanyDocument *doc,
	G_GNUC_UNUSED gpointer user_data)
{
	LspServer *srv = lsp_server_get(doc);

	if (!srv)
		return;

	lsp_sync_text_document_did_close(srv, doc);
	//printf("close document\n");
}


static void destroy_all(void)
{
	lsp_diagnostics_destroy();
	lsp_semtokens_destroy();
	lsp_symbols_destroy();
}


static void stop_and_init_all_servers(void)
{
	lsp_server_stop_all(FALSE);
	lsp_server_init_all();

	destroy_all();

	lsp_sync_init();
	lsp_diagnostics_init();
}


static void restart_all_servers(void)
{
	GeanyDocument *doc = document_get_current();

	stop_and_init_all_servers();

	if (doc)
		lsp_server_get(doc);
}


static void on_document_save(G_GNUC_UNUSED GObject *obj, GeanyDocument *doc,
	G_GNUC_UNUSED gpointer user_data)
{
	LspServer *srv;

	if (g_strcmp0(doc->real_path, lsp_utils_get_config_filename()) == 0)
	{
		stop_and_init_all_servers();
		return;
	}

	if (lsp_server_uses_init_file(doc->real_path))
	{
		stop_and_init_all_servers();
		return;
	}

	srv = lsp_server_get(doc);
	if (!srv)
		return;

	if (!lsp_sync_is_document_open(doc))
	{
		// "new" documents without filename saved for the first time or
		// "save as" performed
		lsp_sync_text_document_did_open(srv, doc);
	}

	lsp_sync_text_document_did_save(srv, doc);
}

/*
static void on_document_before_save_as(G_GNUC_UNUSED GObject *obj, GeanyDocument *doc,
	G_GNUC_UNUSED gpointer user_data)
{
	LspServer *srv = lsp_server_get(doc);

	if (!srv)
		return;

	lsp_sync_text_document_did_close(srv, doc);
}*/


static void on_document_filetype_set(G_GNUC_UNUSED GObject *obj, GeanyDocument *doc,
	GeanyFiletype *filetype_old, G_GNUC_UNUSED gpointer user_data)
{
	LspServer *srv_old;
	LspServer *srv_new;

	// called also when opening documents - without this it would start servers
	// unnecessarily
	if (!lsp_sync_is_document_open(doc))
		return;

	srv_old = lsp_server_get_for_ft(filetype_old);
	srv_new = lsp_server_get(doc);

	if (srv_old == srv_new)
		return;

	if (srv_old)
		// only sends URI so no problem we are using the "new" doc here
		lsp_sync_text_document_did_close(srv_old, doc);

	// might be NULL because lsp_server_get() just launched new server but should
	// be opened once the new server starts
	if (srv_new)
		lsp_sync_text_document_did_open(srv_new, doc);
}


static void on_document_reload(G_GNUC_UNUSED GObject *obj, GeanyDocument *doc,
	G_GNUC_UNUSED gpointer user_data)
{
	LspServer *srv = lsp_server_get(doc);

	if (!srv)
		return;

	lsp_sync_text_document_did_close(srv, doc);
	lsp_sync_text_document_did_open(srv, doc);
}


/* Geany seems to call the "document-activate" signal only for the last tab of
 * and not every document when e.g. opening/closing a project. This is exactly
 * what we need as lsp_server_get(doc) causes start of a single server and only
 * this file is "opened" in that server.
 */
static void on_document_activate(G_GNUC_UNUSED GObject *obj, GeanyDocument *doc,
	G_GNUC_UNUSED gpointer user_data)
{
	LspServer *srv = lsp_server_get(doc);

	if (!srv)
		return;

	// this might not get called for the first time when server gets started because
	// lsp_server_get() returns NULL. However, we also "open" current and modified
	// documents after successful server handshake
	if (!lsp_sync_is_document_open(doc))
		lsp_sync_text_document_did_open(srv, doc);

	lsp_diagnostics_style_current_doc(srv);
	lsp_diagnostics_redraw_current_doc(srv);
	lsp_highlight_style_current_doc(srv);
	lsp_semtokens_style_current_doc(srv);
}


static gboolean on_editor_notify(G_GNUC_UNUSED GObject *obj, GeanyEditor *editor, SCNotification *nt,
	G_GNUC_UNUSED gpointer user_data)
{
	GeanyDocument *doc = editor->document;
	ScintillaObject *sci = editor->sci;

	if (nt->nmhdr.code == SCN_AUTOCSELECTION)
	{
		LspServer *srv = lsp_server_get_if_running(doc);

		if (!srv || !srv->config.autocomplete_enable)
			return FALSE;

		// ignore cursor position change as a result of autocomplete (for highlighting)
		ignore_selection_change = TRUE;

		sci_start_undo_action(editor->sci);
		lsp_autocomplete_item_selected(srv, doc, SSM(sci, SCI_AUTOCGETCURRENT, 0, 0));
		sci_end_undo_action(editor->sci);

		sci_send_command(sci, SCI_AUTOCCANCEL);

		lsp_autocomplete_set_displayed_symbols(NULL);
		return FALSE;
	}
	else if (nt->nmhdr.code == SCN_AUTOCCANCELLED)
	{
		lsp_autocomplete_set_displayed_symbols(NULL);
		lsp_autocomplete_discard_pending_requests();
		return FALSE;
	}
	else if (nt->nmhdr.code == SCN_DWELLSTART)
	{
		LspServer *srv = lsp_server_get(doc);

		// also delivered when other window has focus
		if (!gtk_widget_has_focus(GTK_WIDGET(sci)))
			return FALSE;

		// the event is also delivered for the margin with numbers where position
		// is -1. In addition, at the top of Scintilla window, the event is delivered
		// when mouse is at the menubar place, with y = 0
		if (nt->position < 0 || nt->y == 0)
			return FALSE;

		if (!srv)
			return FALSE;

		if (lsp_signature_showing_calltip(doc))
			;  /* don't cancel signature calltips by accidental hovers */
		else if (srv->config.diagnostics_enable && lsp_diagnostics_has_diag(nt->position))
			lsp_diagnostics_show_calltip(nt->position);
		else if (srv->config.hover_enable)
			lsp_hover_send_request(srv, doc, nt->position);

		return FALSE;
	}
	else if (nt->nmhdr.code == SCN_DWELLEND)
	{
		LspServer *srv = lsp_server_get(doc);
		if (!srv)
			return FALSE;

		if (srv->config.diagnostics_enable)
			lsp_diagnostics_hide_calltip(doc);
		if (srv->config.hover_enable)
			lsp_hover_hide_calltip(doc);

		return FALSE;
	}
	else if (nt->nmhdr.code == SCN_MODIFIED)
	{
		LspServer *srv;

		// lots of SCN_MODIFIED notifications, filter-out those we are not interested in
		if (!(nt->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_BEFOREDELETE | SC_MOD_BEFOREINSERT)))
			return FALSE;

		srv = lsp_server_get(doc);

		if (!srv || !doc->real_path)
			return FALSE;

		// BEFORE insert, BEFORE delete - send the original document
		if (!lsp_sync_is_document_open(doc) &&
			nt->modificationType & (SC_MOD_BEFOREINSERT | SC_MOD_BEFOREDELETE))
		{
			// might happen when the server just started and no interaction with it was
			// possible before
			lsp_sync_text_document_did_open(srv, doc);
		}

		if (nt->modificationType & SC_MOD_INSERTTEXT)  // after insert
		{
			LspPosition pos_start = lsp_utils_scintilla_pos_to_lsp(sci, nt->position);
			LspPosition pos_end = pos_start;
			gchar *text = malloc(nt->length + 1);

			memcpy(text, nt->text, nt->length);
			text[nt->length] = '\0';
			lsp_sync_text_document_did_change(srv, doc, pos_start, pos_end, text);

			g_free(text);
		}
		else if (nt->modificationType & SC_MOD_BEFOREDELETE)  // BEFORE! delete
		{
			LspPosition pos_start = lsp_utils_scintilla_pos_to_lsp(sci, nt->position);
			LspPosition pos_end = lsp_utils_scintilla_pos_to_lsp(sci, nt->position + nt->length);
			gchar *text = g_strdup("");

			lsp_sync_text_document_did_change(srv, doc, pos_start, pos_end, text);

			g_free(text);
		}
	}
	else if (nt->nmhdr.code == SCN_CALLTIPCLICK)
	{
		LspServer *srv = lsp_server_get_if_running(doc);

		if (!srv)
			return FALSE;

		if (srv->config.signature_enable)
		{
			if (nt->position == 1)  /* up arrow */
				lsp_signature_show_prev();
			if (nt->position == 2)  /* down arrow */
				lsp_signature_show_next();
		}
	}
	else if (nt->nmhdr.code == SCN_UPDATEUI)
	{
		LspServer *srv = lsp_server_get_if_running(doc);

		if (!srv)
			return FALSE;

		if (nt->updated & (SC_UPDATE_H_SCROLL | SC_UPDATE_V_SCROLL | SC_UPDATE_SELECTION /* when caret moves */))
		{
			lsp_signature_hide_calltip(doc);
			lsp_hover_hide_calltip(doc);
			lsp_diagnostics_hide_calltip(doc);

			SSM(sci, SCI_AUTOCCANCEL, 0, 0);
		}

		if (srv->config.highlighting_enable && !ignore_selection_change &&
			(nt->updated & SC_UPDATE_SELECTION))
		{
			lsp_highlight_send_request(srv, doc);
		}
		ignore_selection_change = FALSE;
	}
	else if (nt->nmhdr.code == SCN_CHARADDED)
	{
		// don't hightlight while typing
		lsp_highlight_clear(doc);
	}

	return FALSE;
}


static void on_project_open(G_GNUC_UNUSED GObject *obj, GKeyFile *kf,
	G_GNUC_UNUSED gpointer user_data)
{
	gboolean have_project_config;

	project_configuration_type = g_key_file_get_integer(kf, "lsp", "settings_type", NULL);
	project_configuration_file = g_key_file_get_string(kf, "lsp", "config_file", NULL);

	have_project_config = lsp_utils_get_project_config_filename() != NULL;
	gtk_widget_set_sensitive(menu_items.project_config, have_project_config);
	gtk_widget_set_sensitive(menu_items.user_config, !have_project_config);

	stop_and_init_all_servers();
}


static void on_project_close(G_GNUC_UNUSED GObject *obj, G_GNUC_UNUSED gpointer user_data)
{
	g_free(project_configuration_file);
	project_configuration_file = NULL;

	gtk_widget_set_sensitive(menu_items.project_config, FALSE);

	stop_and_init_all_servers();
}


static void on_project_dialog_confirmed(G_GNUC_UNUSED GObject *obj, GtkWidget *notebook,
	G_GNUC_UNUSED gpointer user_data)
{
	const gchar *config_file;
	gboolean have_project_config;

	project_configuration_type = gtk_combo_box_get_active(GTK_COMBO_BOX(project_dialog.settings_type_combo));
	config_file = gtk_entry_get_text(GTK_ENTRY(project_dialog.config_file_entry));
	SETPTR(project_configuration_file, g_strdup(config_file));

	have_project_config = lsp_utils_get_project_config_filename() != NULL;
	gtk_widget_set_sensitive(menu_items.project_config, have_project_config);
	gtk_widget_set_sensitive(menu_items.user_config, !have_project_config);

	restart_all_servers();
}


static void update_sensitivity(LspProjectConfigurationType config)
{
	if (config == ProjectConfigurationType)
		gtk_widget_set_sensitive(project_dialog.path_box, TRUE);
	else
		gtk_widget_set_sensitive(project_dialog.path_box, FALSE);
}


static void on_combo_changed(void)
{
	update_sensitivity(gtk_combo_box_get_active(GTK_COMBO_BOX(project_dialog.settings_type_combo)));
}


static void add_project_properties_tab(GtkWidget *notebook)
{
	GtkWidget *vbox, *hbox;
	GtkWidget *table;
	GtkWidget *label;

	table = gtk_table_new(2, 2, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 6);
	gtk_table_set_col_spacings(GTK_TABLE(table), 12);

	label = gtk_label_new(_("Configuration:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	project_dialog.settings_type_combo = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(project_dialog.settings_type_combo), _("Use user configuration"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(project_dialog.settings_type_combo), _("Use project configuration"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(project_dialog.settings_type_combo), _("Disable LSP Client for project"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(project_dialog.settings_type_combo), project_configuration_type);
	g_signal_connect(project_dialog.settings_type_combo, "changed", on_combo_changed, NULL);

	ui_table_add_row(GTK_TABLE(table), 0, label, project_dialog.settings_type_combo, NULL);

	label = gtk_label_new(_("Configuration file:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	project_dialog.config_file_entry = gtk_entry_new();
	ui_entry_add_clear_icon(GTK_ENTRY(project_dialog.config_file_entry));
	project_dialog.path_box = ui_path_box_new(_("Choose LSP Configuration File"),
		GTK_FILE_CHOOSER_ACTION_OPEN, GTK_ENTRY(project_dialog.config_file_entry));
	gtk_entry_set_text(GTK_ENTRY(project_dialog.config_file_entry),
		project_configuration_file ? project_configuration_file : "");

	update_sensitivity(project_configuration_type);

	ui_table_add_row(GTK_TABLE(table), 1, label, project_dialog.path_box, NULL);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 12);
	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 6);

	label = gtk_label_new(_("LSP Client"));

	project_dialog.properties_tab = vbox;
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox, label);
}


static void on_project_dialog_open(G_GNUC_UNUSED GObject * obj, GtkWidget * notebook,
		G_GNUC_UNUSED gpointer user_data)
{
	if (!project_dialog.properties_tab)
		add_project_properties_tab(notebook);
}


static void on_project_dialog_close(G_GNUC_UNUSED GObject * obj, GtkWidget * notebook,
		G_GNUC_UNUSED gpointer user_data)
{
	if (project_dialog.properties_tab)
	{
		gtk_widget_destroy(project_dialog.properties_tab);
		project_dialog.properties_tab = NULL;
	}
}


static void on_project_save(G_GNUC_UNUSED GObject *obj, GKeyFile *kf,
		G_GNUC_UNUSED gpointer user_data)
{
	g_key_file_set_integer(kf, "lsp", "settings_type", project_configuration_type);
	g_key_file_set_string(kf, "lsp", "config_file",
		project_configuration_file ? project_configuration_file : "");
}


static gboolean on_update_editor_menu(G_GNUC_UNUSED GObject *obj,
	const gchar *word, gint pos, GeanyDocument *doc, gpointer user_data)
{
	last_click_pos = pos;
	return FALSE;
}


PluginCallback plugin_callbacks[] = {
	{"document-new", (GCallback) &on_document_new, FALSE, NULL},
	{"document-open", (GCallback) &on_document_open, FALSE, NULL},
	{"document-close", (GCallback) &on_document_close, FALSE, NULL},
	{"document-reload", (GCallback) &on_document_reload, FALSE, NULL},
	{"document-activate", (GCallback) &on_document_activate, FALSE, NULL},
	{"document-save", (GCallback) &on_document_save, FALSE, NULL},
//	{"document-before-save-as", (GCallback) &on_document_before_save_as, TRUE, NULL},
	{"document-filetype-set", (GCallback) &on_document_filetype_set, FALSE, NULL},
	{"editor-notify", (GCallback) &on_editor_notify, FALSE, NULL},
	{"update-editor-menu", (GCallback) &on_update_editor_menu, FALSE, NULL},
	{"project-open", (GCallback) &on_project_open, FALSE, NULL},
	{"project-close", (GCallback) &on_project_close, FALSE, NULL},
	{"project-save", (GCallback) &on_project_save, FALSE, NULL},
	{"project-dialog-open", (GCallback) &on_project_dialog_open, FALSE, NULL},
	{"project-dialog-confirmed", (GCallback) &on_project_dialog_confirmed, FALSE, NULL},
	{"project-dialog-close", (GCallback) &on_project_dialog_close, FALSE, NULL},
	{NULL, NULL, FALSE, NULL}
};


static gboolean autocomplete_available(GeanyDocument *doc)
{
	LspServerConfig *cfg = lsp_server_get_config(doc);

	if (!cfg)
		return FALSE;

	return lsp_server_is_usable(doc) && cfg->autocomplete_enable;
}


static void autocomplete_perform(GeanyDocument *doc)
{
	LspServer *srv = lsp_server_get(doc);

	if (!srv)
		return;

	lsp_autocomplete_completion(srv, doc);
}


static gboolean calltips_available(GeanyDocument *doc)
{
	LspServerConfig *cfg = lsp_server_get_config(doc);

	if (!cfg)
		return FALSE;

	return lsp_server_is_usable(doc) && cfg->signature_enable;
}


static void calltips_show(GeanyDocument *doc)
{
	LspServer *srv = lsp_server_get(doc);

	if (!srv)
		return;

	lsp_signature_send_request(srv, doc);
}


static gboolean goto_available(GeanyDocument *doc)
{
	LspServerConfig *cfg = lsp_server_get_config(doc);

	if (!cfg)
		return FALSE;

	return lsp_server_is_usable(doc) && cfg->goto_enable;
}


static void goto_perform(GeanyDocument *doc, gint pos, gboolean definition)
{
	if (definition)
		lsp_goto_definition(pos);
	else
		lsp_goto_declaration(pos);
}


static gboolean doc_symbols_available(GeanyDocument *doc)
{
	LspServerConfig *cfg = lsp_server_get_config(doc);

	if (!cfg)
		return FALSE;

	return lsp_server_is_usable(doc) && cfg->document_symbols_enable;
}


static void doc_symbols_request(GeanyDocument *doc, LspSymbolRequestCallback callback, gpointer user_data)
{
	if (doc_symbols_available(doc))
		lsp_symbols_doc_request(doc, callback, user_data);
}


static GPtrArray *doc_symbols_get_cached(GeanyDocument *doc)
{
	return lsp_symbols_doc_get_cached(doc);
}


static gboolean symbol_highlight_available(GeanyDocument *doc)
{
	LspServerConfig *cfg = lsp_server_get_config(doc);

	if (!cfg)
		return FALSE;

	return lsp_server_is_usable(doc) && cfg->semantic_tokens_enable;
}


static void symbol_highlight_request(GeanyDocument *doc, LspSymbolRequestCallback callback, gpointer user_data)
{
	if (symbol_highlight_available(doc))
		lsp_semtokens_send_request(doc, callback, user_data);
}


static const gchar *symbol_highlight_get_cached(GeanyDocument *doc)
{
	return lsp_semtokens_get_cached(doc);
}


static Lsp lsp = {
	.autocomplete_available = autocomplete_available,
	.autocomplete_perform = autocomplete_perform,

	.calltips_available = calltips_available,
	.calltips_show = calltips_show,

	.goto_available = goto_available,
	.goto_perform = goto_perform,

	.doc_symbols_available = doc_symbols_available,
	.doc_symbols_request = doc_symbols_request,
	.doc_symbols_get_cached = doc_symbols_get_cached,

	.symbol_highlight_available = symbol_highlight_available,
	.symbol_highlight_request = symbol_highlight_request,
	.symbol_highlight_get_cached = symbol_highlight_get_cached
};


static void on_open_project_config(void)
{
	gchar *utf8_filename = utils_get_utf8_from_locale(lsp_utils_get_project_config_filename());
	if (utf8_filename)
		document_open_file(utf8_filename, FALSE, NULL, NULL);
	g_free(utf8_filename);
}


static void on_open_user_config(void)
{
	gchar *utf8_filename = utils_get_utf8_from_locale(lsp_utils_get_user_config_filename());
	document_open_file(utf8_filename, FALSE, NULL, NULL);
	g_free(utf8_filename);
}


static void on_open_global_config(void)
{
	gchar *utf8_filename = utils_get_utf8_from_locale(lsp_utils_get_global_config_filename());
	document_open_file(utf8_filename, TRUE, NULL, NULL);
	g_free(utf8_filename);
}


static void on_show_initialize_responses(void)
{
	gchar *resps = lsp_server_get_initialize_responses();
	document_new_file(NULL, filetypes_lookup_by_name("JSON"), resps);
	g_free(resps);
}


static void show_hover_popup(void)
{
	GeanyDocument *doc = document_get_current();
	LspServer *srv = lsp_server_get(doc);

	if (srv)
		lsp_hover_send_request(srv, doc, sci_get_current_position(doc->editor->sci));
}


static void on_rename_done(void)
{
	// TODO: workaround strange behavior of clangd: it doesn't seem to reflect changes
	// in non-open files unless all files are saved and the server is restarted
	lsp_utils_save_all_docs();
	restart_all_servers();
}


static void invoke_kb(guint key_id, gint pos)
{
	GeanyDocument *doc = document_get_current();

	if (!doc)
		return;

	pos = pos >= 0 ? pos : sci_get_current_position(doc->editor->sci);

	switch (key_id)
	{
		case KB_GOTO_DEFINITION:
			lsp_goto_definition(pos);
			break;
		case KB_GOTO_DECLARATION:
			lsp_goto_declaration(pos);
			break;
		case KB_GOTO_TYPE_DEFINITION:
			lsp_goto_type_definition(pos);
			break;

		case KB_GOTO_ANYWHERE:
			lsp_goto_anywhere_for_file();
			break;
		case KB_GOTO_DOC_SYMBOL:
			lsp_goto_anywhere_for_doc();
			break;
		case KB_GOTO_WORKSPACE_SYMBOL:
			lsp_goto_anywhere_for_workspace();
			break;
		case KB_GOTO_LINE:
			lsp_goto_anywhere_for_line();
			break;

		case KB_FIND_REFERENCES:
			lsp_goto_references(pos);
			break;
		case KB_FIND_IMPLEMENTATIONS:
			lsp_goto_implementations(pos);
			break;

		case KB_SHOW_HOVER_POPUP:
			show_hover_popup();
			break;

		case KB_RENAME_IN_FILE:
			lsp_highlight_rename(pos);
			break;

		case KB_RENAME_IN_PROJECT:
			lsp_rename_send_request(pos, on_rename_done);
			break;

		case KB_FORMAT_CODE:
			lsp_format_perform();
			break;

		case KB_RESTART_SERVERS:
			restart_all_servers();
			break;

		default:
			break;
	}
}


static gboolean on_kb_invoked(guint key_id)
{
	invoke_kb(key_id, -1);
	return TRUE;
}


static void on_menu_invoked(GtkWidget *widget, gpointer user_data)
{
	guint key_id = GPOINTER_TO_UINT(user_data);
	invoke_kb(key_id, -1);
}


static void on_context_menu_invoked(GtkWidget *widget, gpointer user_data)
{
	guint key_id = GPOINTER_TO_UINT(user_data);
	invoke_kb(key_id, last_click_pos);
}


static void create_menu_items()
{
	GtkWidget *menu, *item;
	GeanyKeyGroup *group;

	group = plugin_set_key_group(geany_plugin, "lsp", KB_COUNT, on_kb_invoked);

	menu_items.parent_item = gtk_menu_item_new_with_mnemonic(_("_LSP Client"));
	gtk_container_add(GTK_CONTAINER(geany->main_widgets->tools_menu), menu_items.parent_item);

	menu = gtk_menu_new ();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_items.parent_item), menu);

	item = gtk_menu_item_new_with_mnemonic(_("Go to _Definition"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_GOTO_DEFINITION));
	keybindings_set_item(group, KB_GOTO_DEFINITION, NULL, 0, 0, "goto_definition",
		_("Go to definition"), item);

	item = gtk_menu_item_new_with_mnemonic(_("Go to D_eclaration"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_GOTO_DECLARATION));
	keybindings_set_item(group, KB_GOTO_DECLARATION, NULL, 0, 0, "goto_declaration",
		_("Go to declaration"), item);

	item = gtk_menu_item_new_with_mnemonic(_("Go to _Type Definition"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_GOTO_TYPE_DEFINITION));
	keybindings_set_item(group, KB_GOTO_TYPE_DEFINITION, NULL, 0, 0, "goto_type_definition",
		_("Go to type definition"), item);

	gtk_container_add(GTK_CONTAINER(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("Go to _Anywhere..."));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(lsp_goto_anywhere_for_file), NULL);
	keybindings_set_item(group, KB_GOTO_ANYWHERE, NULL, 0, 0, "goto_anywhere",
		_("Go to anywhere"), item);

	item = gtk_menu_item_new_with_mnemonic(_("Go to _Document Symbol..."));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(lsp_goto_anywhere_for_doc), NULL);
	keybindings_set_item(group, KB_GOTO_DOC_SYMBOL, NULL, 0, 0, "goto_doc_symbol",
		_("Go to document symbol"), item);

	item = gtk_menu_item_new_with_mnemonic(_("Go to _Workspace Symbol..."));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(lsp_goto_anywhere_for_workspace), NULL);
	keybindings_set_item(group, KB_GOTO_WORKSPACE_SYMBOL, NULL, 0, 0, "goto_workspace_symbol",
		_("Go to workspace symbol"), item);

	item = gtk_menu_item_new_with_mnemonic(_("Go to _Line..."));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(lsp_goto_anywhere_for_line), NULL);
	keybindings_set_item(group, KB_GOTO_LINE, NULL, 0, 0, "goto_line",
		_("Go to line"), item);

	gtk_container_add(GTK_CONTAINER(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("Find _References"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_FIND_REFERENCES));
	keybindings_set_item(group, KB_FIND_REFERENCES, NULL, 0, 0, "find_references",
		_("Find references"), item);

	item = gtk_menu_item_new_with_mnemonic(_("Find _Implementations"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_FIND_IMPLEMENTATIONS));
	keybindings_set_item(group, KB_FIND_IMPLEMENTATIONS, NULL, 0, 0, "find_implementations",
		_("Find implementations"), item);

	gtk_container_add(GTK_CONTAINER(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("_Rename in File"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_RENAME_IN_FILE));
	keybindings_set_item(group, KB_RENAME_IN_FILE, NULL, 0, 0, "rename_in_file",
		_("Rename in file"), item);

	item = gtk_menu_item_new_with_mnemonic(_("Rename in _Project..."));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_RENAME_IN_PROJECT));
	keybindings_set_item(group, KB_RENAME_IN_PROJECT, NULL, 0, 0, "rename_in_project",
		_("Rename in project"), item);

	item = gtk_menu_item_new_with_mnemonic(_("_Format Code"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_FORMAT_CODE));
	keybindings_set_item(group, KB_FORMAT_CODE, NULL, 0, 0, "format_code",
		_("Format code"), item);

	gtk_container_add(GTK_CONTAINER(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("Show _Hover Popup"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_SHOW_HOVER_POPUP));
	keybindings_set_item(group, KB_SHOW_HOVER_POPUP, NULL, 0, 0, "show_hover_popup",
		_("Show hover popup"), item);

	gtk_container_add(GTK_CONTAINER(menu), gtk_separator_menu_item_new());

	menu_items.project_config = gtk_menu_item_new_with_mnemonic(_("_Project Configuration"));
	gtk_container_add(GTK_CONTAINER(menu), menu_items.project_config);
	g_signal_connect(menu_items.project_config, "activate", G_CALLBACK(on_open_project_config), NULL);

	menu_items.user_config = gtk_menu_item_new_with_mnemonic(_("_User Configuration"));
	gtk_container_add(GTK_CONTAINER(menu), menu_items.user_config);
	g_signal_connect(menu_items.user_config, "activate", G_CALLBACK(on_open_user_config), NULL);

	item = gtk_menu_item_new_with_mnemonic(_("_Global Configuration"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_open_global_config), NULL);

	gtk_container_add(GTK_CONTAINER(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("_Server Initialize Responses"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_show_initialize_responses), NULL);

	gtk_container_add(GTK_CONTAINER(menu), gtk_separator_menu_item_new());

	item = gtk_menu_item_new_with_mnemonic(_("_Restart All Servers"));
	gtk_container_add(GTK_CONTAINER(menu), item);
	g_signal_connect(item, "activate", G_CALLBACK(on_menu_invoked),
		GUINT_TO_POINTER(KB_RESTART_SERVERS));
	keybindings_set_item(group, KB_RESTART_SERVERS, NULL, 0, 0, "restart_all_servers",
		_("Restart all servers"), item);

	gtk_widget_show_all(menu_items.parent_item);

	/* context menu */
	menu_items.separator1 = gtk_separator_menu_item_new();
	gtk_widget_show(menu_items.separator1);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(geany->main_widgets->editor_menu), menu_items.separator1);

	menu_items.format_code = gtk_menu_item_new_with_mnemonic(_("_Format Code (LSP)"));
	gtk_widget_show(menu_items.format_code);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(geany->main_widgets->editor_menu), menu_items.format_code);
	g_signal_connect(menu_items.format_code, "activate", G_CALLBACK(on_context_menu_invoked),
		GUINT_TO_POINTER(KB_FORMAT_CODE));

	menu_items.rename_in_project = gtk_menu_item_new_with_mnemonic(_("Rename in _Project (LSP)..."));
	gtk_widget_show(menu_items.rename_in_project);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(geany->main_widgets->editor_menu), menu_items.rename_in_project);
	g_signal_connect(menu_items.rename_in_project, "activate", G_CALLBACK(on_context_menu_invoked),
		GUINT_TO_POINTER(KB_RENAME_IN_PROJECT));

	menu_items.rename_in_file = gtk_menu_item_new_with_mnemonic(_("_Rename in File (LSP)"));
	gtk_widget_show(menu_items.rename_in_file);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(geany->main_widgets->editor_menu), menu_items.rename_in_file);
	g_signal_connect(menu_items.rename_in_file, "activate", G_CALLBACK(on_context_menu_invoked),
		GUINT_TO_POINTER(KB_RENAME_IN_FILE));

	menu_items.separator2 = gtk_separator_menu_item_new();
	gtk_widget_show(menu_items.separator2);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(geany->main_widgets->editor_menu), menu_items.separator2);

	menu_items.goto_type_def = gtk_menu_item_new_with_mnemonic(_("Go to _Type Definition (LSP)"));
	gtk_widget_show(menu_items.goto_type_def);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(geany->main_widgets->editor_menu), menu_items.goto_type_def);
	g_signal_connect(menu_items.goto_type_def, "activate", G_CALLBACK(on_context_menu_invoked),
		GUINT_TO_POINTER(KB_GOTO_TYPE_DEFINITION));

	menu_items.goto_ref = gtk_menu_item_new_with_mnemonic(_("Find _References (LSP)"));
	gtk_widget_show(menu_items.goto_ref);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(geany->main_widgets->editor_menu), menu_items.goto_ref);
	g_signal_connect(menu_items.goto_ref, "activate", G_CALLBACK(on_context_menu_invoked),
		GUINT_TO_POINTER(KB_FIND_REFERENCES));
}


void plugin_init(G_GNUC_UNUSED GeanyData * data)
{
	plugin_module_make_resident(geany_plugin);

	stop_and_init_all_servers();

	lsp_register(&lsp);
	create_menu_items();
}


void plugin_cleanup(void)
{
	gtk_widget_destroy(menu_items.parent_item);
	gtk_widget_destroy(menu_items.goto_type_def);
	gtk_widget_destroy(menu_items.format_code);
	gtk_widget_destroy(menu_items.rename_in_file);
	gtk_widget_destroy(menu_items.rename_in_project);
	gtk_widget_destroy(menu_items.goto_ref);
	gtk_widget_destroy(menu_items.separator1);
	gtk_widget_destroy(menu_items.separator2);

	lsp_unregister(&lsp);
	lsp_server_stop_all(TRUE);
	destroy_all();
}


void plugin_help (void)
{
	//utils_open_browser("http://plugins.geany.org/projectorganizer.html");
}
