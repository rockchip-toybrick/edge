@echo off

@if "%1"=="" goto usage
@if "%2"=="" goto usage

@echo on
.\dd.exe if=.\Image\boot_linux.img of=%1 bs=4M --size --progress
.\dd.exe if=.\Image\rootfs.img of=%2 bs=4M --size --progress
@echo off

pause
@goto end 
:usage 
@echo Usage: %0 boot_linux_volume_id rootfs_volume_id
@echo Use command "dd.exe --list" to find boot_linux_volume_id and rootfs_volume_id
@echo Example: %0 \\?\Device\HarddiskVolume20  \\?\Device\HarddiskVolume21
:end
