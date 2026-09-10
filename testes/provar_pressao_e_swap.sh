#!/bin/sh
set -eu

# § I. ADVERTENCIA — esta experiência destrói todo conteúdo do dispositivo.
dispositivo=${1:-}
if [ -z "$dispositivo" ] || [ ! -b "$dispositivo" ]; then
    echo "Indique exactamente um dispositivo de blocos existente." >&2
    exit 2
fi
if [ "${CONFIRMAR_DESTRUICAO:-NAO}" != "SIM" ]; then
    echo "Defina CONFIRMAR_DESTRUICAO=SIM para consentir a experiência." >&2
    exit 2
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "A prova de swap reclama privilégios de administrador." >&2
    exit 2
fi

conferir_identidade_do_alvo()
{
    alvo=$1
    nome=$(basename -- "$alvo")
    case "$nome" in ublkb[0-9]*) ;; *) return 1 ;; esac
    origem=$(readlink -f -- "$alvo")
    set -- $(stat -c '%t %T' -- "$alvo")
    major=$(printf '%d' "0x$1")
    minor=$(printf '%d' "0x$2")
    sysfs="/sys/dev/block/$major:$minor"
    [ -e "$sysfs" ] || return 1
    [ "$(readlink -f -- "$sysfs")" = \
        "$(readlink -f -- "/sys/class/block/$nome")" ] || return 1
    if findmnt -rn -S "$origem" >/dev/null 2>&1; then return 1; fi
    if swapon --show=NAME --noheadings 2>/dev/null |
        while read -r swap; do
            [ "$(readlink -f -- "$swap")" = "$origem" ] && exit 0
        done
    then return 1; fi
}

if ! conferir_identidade_do_alvo "$dispositivo"; then
    echo "O alvo não é o ublk descartável conferido pela experiência." >&2
    exit 2
fi
for instrumento in fio mkswap swapon swapoff stress-ng; do
    if ! command -v "$instrumento" >/dev/null 2>&1; then
        echo "Falta o instrumento exterior: $instrumento" >&2
        exit 2
    fi
done

# § II. A integridade precede o offício de swap e ocupa toda a grandeza.
conferir_identidade_do_alvo "$dispositivo" || exit 2
fio --name=prova_integral --filename="$dispositivo" --direct=1 \
    --ioengine=io_uring --rw=randrw --rwmixread=50 --bs=128k \
    --iodepth=32 --size=100% --verify=crc32c --do_verify=1 \
    --verify_fatal=1 --group_reporting

# § III. A armadilha restitue o swap mesmo se a pressão romper a prova.
swap_activado=0
restituir_swap()
{
    if [ "$swap_activado" -eq 1 ]; then
        swapoff "$dispositivo"
    fi
}
trap restituir_swap EXIT INT TERM

conferir_identidade_do_alvo "$dispositivo" || exit 2
mkswap -f "$dispositivo"
conferir_identidade_do_alvo "$dispositivo" || exit 2
swapon --priority -2 "$dispositivo"
swap_activado=1
conferir_identidade_do_alvo "$dispositivo" || exit 2
stress-ng --vm 2 --vm-bytes 80% --verify --timeout 60s
swapoff "$dispositivo"
swap_activado=0
trap - EXIT INT TERM

echo "A integridade e a pressão convergiram. Q.E.D."
