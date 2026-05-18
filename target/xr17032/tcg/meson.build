if 'CONFIG_TCG' not in config_all_accel
  subdir_done()
endif

xr17032_ss.add(files(
  'op_helper.c',
  'tcg_cpu.c',
  'translate.c',
))

xr17032_system_ss.add(files(
  'cr_helper.c',
  'tb_helper.c',
))
