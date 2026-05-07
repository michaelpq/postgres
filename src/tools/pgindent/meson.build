# Copyright (c) 2024-2026, PostgreSQL Global Development Group

pgindent_sources = files(
  'args.c',
  'err.c',
  'indent_core.c',
  'io.c',
  'lexi.c',
  'parse.c',
  'pr_comment.c',
)

if host_system == 'windows'
  pgindent_sources += rc_bin_gen.process(win32ver_rc, extra_args: [
    '--NAME', 'pgindent',
    '--FILEDESC', 'pgindent - PostgreSQL source code indenter'])
endif

pgindent_exe = executable('pgindent',
  pgindent_sources,
  c_args: cflags_no_missing_var_decls,
  dependencies: [frontend_code],
  include_directories: include_directories('.'),
  kwargs: default_bin_args + {
    'install': false,
  },
)
bin_targets += pgindent_exe
