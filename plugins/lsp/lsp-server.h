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

#ifndef LSP_SERVER_H
#define LSP_SERVER_H 1

#include <geanyplugin.h>

#include <gio/gio.h>
#include <jsonrpc-glib.h>


typedef struct
{
	gboolean show_server_stderr;
	gchar *rpc_log;
	gchar *initialization_options_file;
	gboolean use_outside_project_dir;
	gboolean use_without_project;

	gboolean autocomplete_enable;
	gchar **autocomplete_trigger_sequences;
	gboolean autocomplete_use_label;
	gboolean autocomplete_apply_additional_edits;
	gint autocomplete_window_max_entries;
	gint autocomplete_window_max_displayed;

	gboolean diagnostics_enable;
	gchar *diagnostics_error_style;
	gchar *diagnostics_warning_style;
	gchar *diagnostics_info_style;
	gchar *diagnostics_hint_style;

	gchar *formatting_options_file;

	gboolean hover_enable;
	gint hover_popup_max_lines;

	gboolean signature_enable;

	gboolean goto_enable;

	gboolean document_symbols_enable;

	gboolean semantic_tokens_enable;

	gboolean highlighting_enable;
	gchar *highlighting_style;

} LspServerConfig;


typedef struct
{
	gint type;  // 0: use stream, 1: stdout, 2: stderr
	GFileOutputStream *stream;
} LspLogInfo;


typedef struct LspServer
{
	gchar *cmd;
	gchar *ref_lang;

	JsonrpcClient *rpc_client;
	GSubprocess *process;
	GIOStream *stream;
	LspLogInfo log;

	struct LspServer *referenced;
	gboolean not_used;
	gboolean startup_shutdown;
	guint restarts;

	LspServerConfig config;

	GSList *progress_ops;

	gchar *autocomplete_trigger_chars;
	gchar *signature_trigger_chars;
	gchar *initialize_response;
	gboolean use_incremental_sync;
	gboolean supports_workspace_symbols;

	guint64 semantic_token_mask;
} LspServer;


LspServer *lsp_server_get(GeanyDocument *doc);
LspServer *lsp_server_get_for_ft(GeanyFiletype *ft);
LspLogInfo lsp_server_get_log_info(JsonrpcClient *client);
LspServer *lsp_server_get_if_running(GeanyDocument *doc);
LspServerConfig *lsp_server_get_config(GeanyDocument *doc);
gboolean lsp_server_is_usable(GeanyDocument *doc);

void lsp_server_stop_all(gboolean wait);
void lsp_server_init_all(void);

gboolean lsp_server_uses_init_file(gchar *path);

gchar *lsp_server_get_initialize_responses(void);

#endif  /* LSP_SERVER_H */
