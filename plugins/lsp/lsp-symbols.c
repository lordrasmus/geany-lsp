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

#include "lsp/lsp-symbols.h"
#include "lsp/lsp-client.h"
#include "lsp/lsp-utils.h"
#include "lsp/lsp-sync.h"
#include "lsp/lsp-tm-tag.h"


typedef struct {
	GeanyDocument *doc;
	LspSymbolRequestCallback callback;
	gpointer user_data;
} LspSymbolUserData;

typedef struct {
	gint ft_id;
	LspWorkspaceSymbolRequestCallback callback;
	gpointer user_data;
} LspWorkspaceSymbolUserData;


extern GeanyPlugin *geany_plugin;

//TODO: possibly cache symbols of multiple files
static GPtrArray *cached_symbols;
static gchar *cached_symbols_fname;


void lsp_symbols_destroy(void)
{
	if (cached_symbols)
		g_ptr_array_free(cached_symbols, TRUE);
	cached_symbols = NULL;
	g_free(cached_symbols_fname);
	cached_symbols_fname = NULL;
}


static void parse_symbols(GPtrArray *symbols, GVariant *symbol_variant, const gchar *scope,
	const gchar *scope_sep, gboolean workspace)
{
	GVariant *member = NULL;
	GVariantIter iter;

	g_variant_iter_init(&iter, symbol_variant);

	while (g_variant_iter_loop(&iter, "v", &member))
	{
		TMTag *tag;
		const gchar *name = NULL;
		const gchar *detail = NULL;
		const gchar *uri = NULL;
		const gchar *container_name = NULL;
		GVariant *loc_variant = NULL;
		GVariant *children = NULL;
		gchar *uri_str = NULL;
		gint64 kind = 0;
		gint line_num = 0;

		if (!JSONRPC_MESSAGE_PARSE(member, "name", JSONRPC_MESSAGE_GET_STRING(&name)))
			continue;
		if (!JSONRPC_MESSAGE_PARSE(member, "kind", JSONRPC_MESSAGE_GET_INT64(&kind)))
			continue;

		if (JSONRPC_MESSAGE_PARSE(member, "selectionRange", JSONRPC_MESSAGE_GET_VARIANT(&loc_variant)))
		{
			LspRange range = lsp_utils_parse_range(loc_variant);
			line_num = range.start.line;
		}
		else if (JSONRPC_MESSAGE_PARSE(member, "range", JSONRPC_MESSAGE_GET_VARIANT(&loc_variant)))
		{
			LspRange range = lsp_utils_parse_range(loc_variant);
			line_num = range.start.line;
		}
		else if (JSONRPC_MESSAGE_PARSE(member, "location", JSONRPC_MESSAGE_GET_VARIANT(&loc_variant)))
		{
			LspLocation *loc = lsp_utils_parse_location(loc_variant);
			if (loc)
			{
				line_num = loc->range.start.line;
				if (loc->uri)
					uri_str = g_strdup(loc->uri);
				lsp_utils_free_lsp_location(loc);
			}
		}
		else if (!workspace)
		{
			if (loc_variant)
				g_variant_unref(loc_variant);
			continue;
		}

		if (workspace)
		{
			JSONRPC_MESSAGE_PARSE(member, "containerName", JSONRPC_MESSAGE_GET_STRING(&container_name));

			if (!uri_str)
			{
				if (JSONRPC_MESSAGE_PARSE(member, "location", "{",
						"uri", JSONRPC_MESSAGE_GET_STRING(&uri),
					"}"))
				{
					if (uri)
						uri_str = g_strdup(uri);
				}

				if (!uri_str)
					continue;
			}
		}

		JSONRPC_MESSAGE_PARSE(member, "detail", JSONRPC_MESSAGE_GET_STRING(&detail));

		tag = lsp_tm_tag_new();
		tag->name = g_strdup(name);
		tag->line = line_num + 1;
		tag->type = kind;
		tag->arglist = detail ? g_strdup(detail) : NULL;

		if (scope)
			tag->scope = g_strdup(scope);
		else if (container_name)
			tag->scope = g_strdup(container_name);

		if (uri_str)
		{
			// TODO: total hack, just storing path "somewhere"
			tag->inheritance = lsp_utils_get_real_path_from_uri_utf8(uri_str);
		}

		g_ptr_array_add(symbols, tag);

		if (JSONRPC_MESSAGE_PARSE(member, "children", JSONRPC_MESSAGE_GET_VARIANT(&children)))
		{
			gchar *new_scope;

			if (scope)
				new_scope = g_strconcat(scope, scope_sep, tag->name, NULL);
			else
				new_scope = g_strdup(tag->name);
			parse_symbols(symbols, children, new_scope, scope_sep, FALSE);
			g_free(new_scope);
		}

		if (loc_variant)
			g_variant_unref(loc_variant);
		if (children)
			g_variant_unref(children);
		g_free(uri_str);
	}
}


static void symbols_cb(GObject *object, GAsyncResult *result, gpointer user_data)
{
	JsonrpcClient *self = (JsonrpcClient *)object;
	GVariant *return_value = NULL;
	LspSymbolUserData *data = user_data;

	if (lsp_client_call_finish(self, result, &return_value))
	{
		//printf("%s\n\n\n", lsp_utils_json_pretty_print(return_value));

		if (data->doc == document_get_current())
		{
			lsp_symbols_destroy();
			cached_symbols = g_ptr_array_new_full(0, (GDestroyNotify)lsp_tm_tag_unref);
			cached_symbols_fname = g_strdup(data->doc->real_path);

			parse_symbols(cached_symbols, return_value, NULL,
				symbols_get_context_separator(data->doc->file_type->id), FALSE);
		}

		if (return_value)
			g_variant_unref(return_value);
	}

	data->callback(data->user_data);

	g_free(user_data);
}


GPtrArray *lsp_symbols_doc_get_cached(GeanyDocument *doc)
{
	if (g_strcmp0(doc->real_path, cached_symbols_fname) == 0)
		return cached_symbols;

	return NULL;
}


static gboolean retry_cb(gpointer user_data)
{
	LspSymbolUserData *data = user_data;

	//printf("retrying symbols\n");
	if (data->doc == document_get_current())
	{
		LspServer *srv = lsp_server_get(data->doc);
		if (!lsp_server_is_usable(data->doc))
			;  // server died or misconfigured
		else if (!srv)
		{
			// should be successful now
			lsp_symbols_doc_request(data->doc, data->callback, data->user_data);
			g_free(data);
			return FALSE;
		}
	}

	// server shut down or document not current any more
	g_free(data);
	return FALSE;
}


void lsp_symbols_doc_request(GeanyDocument *doc, LspSymbolRequestCallback callback,
	gpointer user_data)
{
	LspServer *server = lsp_server_get(doc);
	LspSymbolUserData *data = g_new0(LspSymbolUserData, 1);
	GVariant *node;
	gchar *doc_uri;

	data->user_data = user_data;
	data->doc = doc;
	data->callback = callback;

	if (!server)
	{
		// happens when Geany and LSP server started - we cannot send the request yet
		plugin_timeout_add(geany_plugin, 300, retry_cb, data);
		return;
	}

	doc_uri = lsp_utils_get_doc_uri(doc);

	/* Geany requests symbols before firing "document-activate" signal so we may
	 * need to request document opening here */
	if (!lsp_sync_is_document_open(doc))
		lsp_sync_text_document_did_open(server, doc);

	node = JSONRPC_MESSAGE_NEW (
		"textDocument", "{",
			"uri", JSONRPC_MESSAGE_PUT_STRING(doc_uri),
		"}"
	);

	//printf("%s\n\n\n", lsp_utils_json_pretty_print(node));

	lsp_client_call_async(server->rpc_client, "textDocument/documentSymbol", node,
		symbols_cb, data);

	g_free(doc_uri);
	g_variant_unref(node);
}


static void workspace_symbols_cb(GObject *object, GAsyncResult *result, gpointer user_data)
{
	JsonrpcClient *self = (JsonrpcClient *)object;
	GVariant *return_value = NULL;
	LspWorkspaceSymbolUserData *data = user_data;
	GPtrArray *ret = g_ptr_array_new_full(0, (GDestroyNotify)lsp_tm_tag_unref);

	if (lsp_client_call_finish(self, result, &return_value))
	{
		//printf("%s\n\n\n", lsp_utils_json_pretty_print(return_value));

		//scope separator doesn't matter here
		parse_symbols(ret, return_value, NULL, "", TRUE);

		if (return_value)
			g_variant_unref(return_value);
	}

	data->callback(ret, data->user_data);

	g_ptr_array_free(ret, TRUE);
	g_free(user_data);
}


void lsp_symbols_workspace_request(GeanyFiletype *ft, const gchar *query,
	LspWorkspaceSymbolRequestCallback callback, gpointer user_data)
{
	LspServer *server = lsp_server_get_for_ft(ft);
	LspWorkspaceSymbolUserData *data;
	GVariant *node;

	if (!server)
		return;

	data = g_new0(LspWorkspaceSymbolUserData, 1);
	data->user_data = user_data;
	data->callback = callback;
	data->ft_id = ft->id;

	node = JSONRPC_MESSAGE_NEW (
		"query", JSONRPC_MESSAGE_PUT_STRING(query)
	);

	//printf("%s\n\n\n", lsp_utils_json_pretty_print(node));

	lsp_client_call_async(server->rpc_client, "workspace/symbol", node,
		workspace_symbols_cb, data);

	g_variant_unref(node);
}
