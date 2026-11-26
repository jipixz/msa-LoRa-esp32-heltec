# Sondeo de la Pi para la consola (se manda por "ssh pi bash -s"; no se instala nada).
# Por cada puerto serie: dispositivo USB detrás (VID:PID, producto, serie, ruta), driver y qué
# procesos lo tienen abierto. Luego los demás USB, el estado del hub interno y msa-back.
for d in /dev/ttyUSB* /dev/ttyACM*; do
  [ -e "$d" ] || continue
  n=$(basename "$d")
  p=$(readlink -f "/sys/class/tty/$n/device")
  while [ "$p" != "/" ] && [ ! -f "$p/idVendor" ]; do p=$(dirname "$p"); done
  drv=$(basename "$(readlink -f "/sys/class/tty/$n/device/driver")" 2>/dev/null)
  usa=""
  for pid in $(find /proc/[0-9]*/fd -lname "$d" 2>/dev/null | cut -d/ -f3 | sort -u); do
    cmd=$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | cut -c1-80)
    usa="$usa$pid:$(cat "/proc/$pid/comm" 2>/dev/null):$cmd;"
  done
  echo "TTY|$d|$(cat "$p/idVendor" 2>/dev/null):$(cat "$p/idProduct" 2>/dev/null)|$(cat "$p/product" 2>/dev/null)|$(cat "$p/manufacturer" 2>/dev/null)|$(cat "$p/serial" 2>/dev/null)|$(basename "$p")|$drv|$usa"
done
lsusb | grep -viE "root hub" | sed "s/^/USB /"
echo "HUB $(cat /sys/bus/usb/devices/1-1/power/runtime_status 2>/dev/null) $(cat /sys/bus/usb/devices/1-1/power/control 2>/dev/null)"
pgrep -f "msa-back" >/dev/null && echo "MSA activo" || echo "MSA detenido"
