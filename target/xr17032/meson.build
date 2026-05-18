gen = decodetree.process('insns.decode')

xr17032_ss = ss.source_set()
xr17032_ss.add(gen)
xr17032_ss.add(files(
  'cpu.c',
  'disas.c',
))

xr17032_user_ss = ss.source_set()
xr17032_user_ss.add(files('gdbstub.c'))

xr17032_common_system_ss = ss.source_set()
xr17032_common_system_ss.add(files('gdbstub.c'))

xr17032_system_ss = ss.source_set()
xr17032_system_ss.add(files(
  'arch_dump.c',
  'cpu_helper.c',
  'cr.c',
  'xr17032-qmp-cmds.c',
  'machine.c',
))

subdir('tcg')

target_arch += {'xr17032': xr17032_ss}
target_user_arch += {'xr17032': xr17032_user_ss}
target_system_arch += {'xr17032': xr17032_system_ss}
target_common_system_arch += {'xr17032': xr17032_common_system_ss}
