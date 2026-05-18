xr17032_ss = ss.source_set()
xr17032_ss.add(files('boot.c',))
xr17032_ss.add(when: 'CONFIG_XR17032_VIRT', if_true: files('virt.c'))

hw_arch += {'xr17032': xr17032_ss}
