# Kronos OS v0.1

32-bit protected-mode x86 operating system with FAT12 filesystem, VGA mode 13h GUI, multitasking, IPC, and neural network tools.

## Features

- **Graphical desktop** with taskbar and window manager
- **Terminal** with command history, calculator, file management
- **Text Editor** with Ctrl+S/O/Q shortcuts
- **Paint** with 15 colors and variable brush
- **FORTH interpreter** (stack-based REPL)
- **Neural network engine** — create, train, save, infer MLPs
- **Dataset manager** — build and manage training datasets
- **IPC** — inter-process messaging between tasks
- **Multi-language UI** — EN, ES, FR, DE, PT (`LANG` command)
- **Login screen** with persistent password (PASS.SYS)

## Build

Requires:
- `i686-linux-musl-gcc` cross-compiler
- NASM
- Python 3
- Linux or WSL

```bash
bash build_full.sh
```

Output: `cortexos.img` (1.44 MB FAT12 floppy)

## Run

### QEMU
```bash
qemu-system-i386 -fda cortexos.img -m 64
```

### VirtualBox
See `website/setup-vbox.bat` (Windows) or `website/setup-vbox.sh` (Linux/Mac).

The bootloader auto-detects VirtualBox's missing INT 13h extensions and falls back to CHS.

## Commands

| Command | Description |
|---------|-------------|
| HELP | Show help |
| CALC a+b | Basic arithmetic |
| INFO | System info |
| CLEAR | Clear terminal |
| DIR | List files |
| DEL file | Delete file |
| REN old new | Rename file |
| EDIT file | Open editor |
| FORTH | FORTH REPL |
| PAINT | Drawing app |
| NEURAL | NN monitor |
| NN ... | NN commands |
| DS ... | Dataset manager |
| MSG SEND/RECV | IPC |
| MODEL ... | Model registry |
| PS | List tasks |
| LANG ES | Switch language |

## License

MIT
