Example output:

    wdeps.exe (286.00kB)
        libboost_filesystem-mt.dll (108.00kB)
        libboost_system-mt.dll (23.00kB)
        libgcc_s_seh-1.dll (79.00kB)
        libwinpthread-1.dll (52.50kB)
        libstdc++-6.dll (1.36MB)
        libboost_program_options-mt.dll (362.00kB)
    
    Total: 2.25MB
    
Command-line options:

    Usage: wdeps [options] <input>
    
    Options:
      --copy dir            If specified, copy all dependencies to the specified
                            directory.
      --force               When used with --copy, overwrite existing files.
      --all                 When used with --copy, also include the input file.
      --tree                Display the dependencies as a tree (each dependency
                            will only be expanded once).
      --system              Include system dependencies, doesn't affect `--copy`,
                            system dependencies are not recursed into.
      --path                Include the full path to the dependencies in the list.
      --help                Print this help message.
      --input file          The exe/dll file for which to show dependencies.
