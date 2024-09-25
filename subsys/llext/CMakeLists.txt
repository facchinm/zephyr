if(CONFIG_LLEXT)
  zephyr_library()

  # For strnlen()
  zephyr_library_compile_definitions(-D_POSIX_C_SOURCE=200809L)

  zephyr_library_sources(
		llext.c
		llext_mem.c
		llext_load.c
		llext_link.c
		buf_loader.c
	)
  zephyr_library_sources_ifdef(CONFIG_LLEXT_SHELL shell.c)

  # The llext_export.c file must be linked late to prevent the weak symbols
  # it defines from overriding actual symbols defined in the other libs.
  zephyr_library_named(llext_exports)
  zephyr_library_sources(llext_export.c)
  zephyr_library_property(LATE_LINK TRUE)
endif()
