plugin_inc = [include_directories('..', '../src', '../src/tagmanager', 'lsp/json-glib', 'lsp/jsonrpc-glib', 'lsp')]
plugin_inc += iscintilla

plugins = [
	'Demoplugin',
	'Demoproxy',
	'Classbuilder',
	'HTMLChars',
	'Export',
	'SaveActions',
	'FileBrowser',
	'SplitWindow'
]

plugin_suffix = []
if (host_machine.system() == 'darwin')
	plugin_suffix = 'so'  # use "so" instead of "dylib" so Geany finds plugins correctly
endif

foreach plugin : plugins
	id = plugin.to_lower()
	skip_install = id.startswith('demo')
	shared_module(id,
		id + '.c',
		name_prefix: '', # "lib" seems to be the default prefix
		name_suffix: plugin_suffix,
		link_with: libgeany,
		include_directories: plugin_inc,
		c_args: [def_cflags, '-DG_LOG_DOMAIN="'+plugin+'"'],
		dependencies: [deps, dep_libgeany],
		install_dir: join_paths(prefix, get_option('libdir'), 'geany'),
		install: not skip_install
	)
endforeach


lsp_conf = [
	'lsp/data/lsp.conf',
]
install_data(sources: lsp_conf, install_dir: cdata.get('GEANY_DATA_DIR') + '/lsp')


shared_module('lsp',
	'lsp/json-glib/json-array.c',
	'lsp/json-glib/json-builder.c',
	'lsp/json-glib/json-debug.c',
	'lsp/json-glib/json-gboxed.c',
	'lsp/json-glib/json-generator.c',
	'lsp/json-glib/json-gobject.c',
	'lsp/json-glib/json-gvariant.c',
	'lsp/json-glib/json-node.c',
	'lsp/json-glib/json-object.c',
	'lsp/json-glib/json-parser.c',
	'lsp/json-glib/json-path.c',
	'lsp/json-glib/json-reader.c',
	'lsp/json-glib/json-scanner.c',
	'lsp/json-glib/json-serializable.c',
	'lsp/json-glib/json-utils.c',
	'lsp/json-glib/json-value.c',

	'lsp/jsonrpc-glib/jsonrpc-client.c',
	'lsp/jsonrpc-glib/jsonrpc-input-stream.c',
	'lsp/jsonrpc-glib/jsonrpc-message.c',
	'lsp/jsonrpc-glib/jsonrpc-output-stream.c',
	'lsp/jsonrpc-glib/jsonrpc-server.c',
	'lsp/jsonrpc-glib/jsonrpc-marshalers.c',

	'lsp/lsp-autocomplete.c',
	'lsp/lsp-main.c',
	'lsp/lsp-server.c',
	'lsp/lsp-sync.c',
	'lsp/lsp-client.c',
	'lsp/lsp-diagnostics.c',
	'lsp/lsp-hover.c',
	'lsp/lsp-signature.c',
	'lsp/lsp-log.c',
	'lsp/lsp-goto.c',
	'lsp/lsp-progress.c',
	'lsp/lsp-symbols.c',
	'lsp/lsp-symbol-kinds.c',
	'lsp/lsp-semtokens.c',
	'lsp/lsp-goto-panel.c',
	'lsp/lsp-goto-anywhere.c',
	'lsp/lsp-tm-tag.c',
	'lsp/lsp-format.c',
	'lsp/lsp-highlight.c',
	'lsp/lsp-rename.c',
	'lsp/lsp-command.c',
	'lsp/lsp-code-lens.c',
	'lsp/lsp-utils.c',
	name_prefix: '',  # "lib" seems to be the default prefix
	link_with: libgeany,
	include_directories: plugin_inc,
	c_args: [def_cflags, '-DG_LOG_DOMAIN="LSP"', '-DJSON_COMPILATION', '-DJSONRPC_GLIB_COMPILATION'],
	dependencies: [deps, dep_libgeany],
	install_dir: join_paths(prefix, get_option('libdir'), 'geany'),
	install: true
)
