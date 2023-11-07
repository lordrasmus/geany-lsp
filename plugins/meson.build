plugin_inc = [include_directories('..', '../src', '../src/tagmanager')]
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
	'lsp/lsp-semtokens.c',
	'lsp/lsp-utils.c',
	name_prefix: '',  # "lib" seems to be the default prefix
	link_with: libgeany,
	include_directories: plugin_inc,
	c_args: [def_cflags, '-DG_LOG_DOMAIN="LSP"'],
	dependencies: [deps, dep_jsonrpc, dep_libgeany],
	install_dir: join_paths(prefix, get_option('libdir'), 'geany'),
	install: true
)
