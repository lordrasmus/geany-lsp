geany-lsp
---------

This is a temporary home for Geany (plus LSP patches) and the LSP plugin combined
in a single repository. Once the patches are merged to Geany, the LSP plugin will
be separated from the Geany repository.

So far, this plugin only supports the following features:
* diagnostics (underlined errors in the edited document plus error message on mouse hover)
* autocompletion
* signature (aka calltips)
* goto tag definition/declaration
* hover (hover over identifiers to reveal a popup with more information about the symbol)

These features can be enabled/disabled one by one and possibly combined with
their tag manager counterparts from Geany. There are various configuration options
that can be changed by editing the file at Tools->LSP Client->User Configuration
(this file contains comments describing more details). It is also possible to have
a per-project configuration file.

So far, this plugin has only been tested with the following servers:
* `clangd` - C, C++ (`sudo apt install clangd`)
* `gopls` - Go (`sudo apt install gopls`)
* `pylsp` - Python (`sudo apt install python3-pylsp` - there are also additional
modules starting with `python3-pylsp` on debian and the server's capabilities
depend on presence of other tools on the system)
The default configuration comes with some sane defaults for these servers.

This is how to test the plugin on Geany sources using clangd:
1. Install clangd (e.g. `sudo apt install clangd` on Debian).
2. Build and install Geany with the LSP plugin from this repo.
3. Enable the plugin.
4. clangd depends on `compile_commands.json` to know how the project was bult. By
default, this file should be in the `build` subdirectory of the project. It can
be generated by `meson` - `meson setup build` will create it at the right place.
Alternatively, one can use [Bear](https://github.com/rizsotto/Bear) (`sudo apt install bear`)
and generate this file using autotools by e.g. `bear -- make -j 9` (and has to be
moved to the `build` subdirectory afterwards).
5. Create a new project with the base path pointing to the geany root directory.
The base path is used by the plugin to define the directory of the project - this
is necessary for clangd for instance, but some other LSP servers like pylsp
don't need any project directory.

At this point, the LSP features mentioned above should start working.

