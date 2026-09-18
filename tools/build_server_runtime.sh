#!/bin/sh
set -eu

ALPINE_VERSION=3.24.1
ARCH=x86_64
BASE_URL=https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/$ARCH
WORK=build/server
ROOT=$WORK/rootfs
TARBALL=$WORK/minirootfs.tar.gz

rm -rf $WORK
mkdir -p $ROOT

curl -fL "$BASE_URL/alpine-minirootfs-$ALPINE_VERSION-$ARCH.tar.gz" -o $TARBALL
tar -xzf $TARBALL -C $ROOT

printf '%s\n' \
  https://dl-cdn.alpinelinux.org/alpine/v3.24/main \
  https://dl-cdn.alpinelinux.org/alpine/v3.24/community \
  > $ROOT/etc/apk/repositories

cp /etc/resolv.conf $ROOT/etc/resolv.conf || true

proot -R $ROOT -b /proc:/proc -b /sys:/sys -b /dev:/dev /sbin/apk add --no-cache \
  ca-certificates curl git openssh-server \
  iproute2 iptables kmod \
  nodejs npm python3 py3-pip \
  tailscale wpa_supplicant wine xvfb-run linux-virt linux-firmware-intel

mkdir -p $ROOT/etc/steveos $ROOT/var/lib/tailscale $ROOT/opt/discord-bot

cat > $ROOT/etc/steveos/server.conf <<'EOF'
SERVER_HOSTNAME=steveos-server
DISCORD_ENABLE=0
DISCORD_RUNTIME=node
DISCORD_BOT_REPO=
DISCORD_START="node index.js"
DISCORD_TOKEN=
WIFI_ENABLE=0
WIFI_SSID=
WIFI_PASSWORD=
TAILSCALE_ENABLE=0
TAILSCALE_MODE=normal
TAILSCALE_AUTHKEY=
TAILSCALE_ADVERTISE_ROUTES=
TAILSCALE_EXIT_NODE=0
SSH_ENABLE=0
EOF

cat > $ROOT/usr/local/bin/steveos-run-exe <<'EOF'
#!/bin/sh
set -eu

[ "$#" -ge 1 ] || {
    echo "usage: runexe /efi/SteveOS/Apps/program.exe"
    exit 2
}

EXE=$1
case "$EXE" in
    *.exe|*.EXE) ;;
    *) echo "not a Windows .exe: $EXE"; exit 2 ;;
esac

[ -f "$EXE" ] || {
    echo "EXE not found: $EXE"
    exit 1
}

export WINEPREFIX=${WINEPREFIX:-/var/lib/wine}
export WINEDEBUG=${WINEDEBUG:--all}
mkdir -p "$WINEPREFIX"

if command -v xvfb-run >/dev/null 2>&1; then
    exec xvfb-run -a -s "-screen 0 1280x720x24" wine "$EXE"
fi

exec wine "$EXE"
EOF

cat > $ROOT/init <<'EOF'
#!/bin/sh
set -eu

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts /dev/net /run /run/tailscale /efi
mount -t devpts devpts /dev/pts -o gid=5,mode=620 2>/dev/null || true
mdev -s 2>/dev/null || true
[ -e /dev/net/tun ] || mknod /dev/net/tun c 10 200 2>/dev/null || true

for mod in tun e1000e r8169 igc iwlwifi; do
    modprobe "$mod" 2>/dev/null || true
done

mounted_server_volume=0
for dev in /dev/nvme*n1p* /dev/sd*[0-9] /dev/mmcblk*p*; do
    [ -e "$dev" ] || continue
    if mount -t vfat "$dev" /efi 2>/dev/null; then
        if [ -f /efi/SteveOS/Server/server-initramfs.img ] || [ -f /efi/SteveOS/Server/server.conf ]; then
            mounted_server_volume=1
            break
        fi
        umount /efi 2>/dev/null || true
    fi
done
if [ "$mounted_server_volume" != 1 ]; then
    echo "SteveOS Server volume not found"
fi

CONF=/etc/steveos/server.conf
[ -f /efi/SteveOS/Server/server.conf ] && CONF=/efi/SteveOS/Server/server.conf
. "$CONF"

hostname "$SERVER_HOSTNAME" 2>/dev/null || true

if [ "$WIFI_ENABLE" = 1 ] && [ -n "$WIFI_SSID" ]; then
    mkdir -p /etc/wpa_supplicant
    cat > /etc/wpa_supplicant/steveos.conf <<WPAEOF
ctrl_interface=/run/wpa_supplicant
update_config=0
network={
    ssid="$WIFI_SSID"
    psk="$WIFI_PASSWORD"
}
WPAEOF
    for sysif in /sys/class/net/*; do
        iface=$(basename "$sysif")
        [ "$iface" = lo ] && continue
        if [ -d "/sys/class/net/$iface/wireless" ]; then
            ip link set "$iface" up 2>/dev/null || true
            wpa_supplicant -B -i "$iface" -c /etc/wpa_supplicant/steveos.conf 2>/dev/null || true
        fi
    done
fi

for attempt in 1 2 3 4 5 6 7 8 9 10; do
    linked=0
    for sysif in /sys/class/net/*; do
        iface=$(basename "$sysif")
        [ "$iface" = lo ] && continue
        ip link set "$iface" up 2>/dev/null || true
        udhcpc -q -n -i "$iface" 2>/dev/null && linked=1 || true
    done
    [ "$linked" = 1 ] && break
    sleep 1
done

if [ -d /efi/SteveOS/Server/bot ]; then
    rm -rf /opt/discord-bot
    mkdir -p /opt/discord-bot
    cp -a /efi/SteveOS/Server/bot/. /opt/discord-bot/
fi

start_tailscale() {
    [ "$TAILSCALE_ENABLE" = 1 ] || return 0
    modprobe tun 2>/dev/null || true
    mkdir -p /var/lib/tailscale /run/tailscale
    if [ "$TAILSCALE_MODE" = userspace ]; then
        tailscaled --tun=userspace-networking --socks5-server=localhost:1055 --outbound-http-proxy-listen=localhost:1056 --state=/var/lib/tailscale/tailscaled.state --socket=/run/tailscale/tailscaled.sock &
    else
        tailscaled --state=/var/lib/tailscale/tailscaled.state --socket=/run/tailscale/tailscaled.sock &
    fi
    for i in 1 2 3 4 5 6 7 8 9 10; do
        [ -S /run/tailscale/tailscaled.sock ] && break
        sleep 1
    done
    ARGS=
    [ -n "$TAILSCALE_ADVERTISE_ROUTES" ] && ARGS="$ARGS --advertise-routes=$TAILSCALE_ADVERTISE_ROUTES"
    [ "$TAILSCALE_EXIT_NODE" = 1 ] && ARGS="$ARGS --advertise-exit-node"
    if [ -n "$TAILSCALE_AUTHKEY" ]; then
        tailscale --socket=/run/tailscale/tailscaled.sock up \
          --auth-key="$TAILSCALE_AUTHKEY" \
          --hostname="$SERVER_HOSTNAME" $ARGS || true
    else
        echo "Tailscale enabled without an auth key. Run: tailscale up"
    fi
}

start_discord() {
    [ "$DISCORD_ENABLE" = 1 ] || return 0
    mkdir -p /opt/discord-bot
    cd /opt/discord-bot

    if [ -n "$DISCORD_BOT_REPO" ] && [ ! -f package.json ] && [ ! -f requirements.txt ]; then
        git clone --depth 1 "$DISCORD_BOT_REPO" /opt/discord-bot/repo
        cp -a /opt/discord-bot/repo/. /opt/discord-bot/
    fi

    if [ -f package.json ]; then
        npm install --omit=dev
    elif [ -f requirements.txt ]; then
        python3 -m venv /opt/discord-bot/.venv
        /opt/discord-bot/.venv/bin/pip install -r requirements.txt
    fi

    while true; do
        if [ "$DISCORD_RUNTIME" = python ]; then
            if [ -x /opt/discord-bot/.venv/bin/python ]; then
                sh -c "$DISCORD_START" || true
            else
                sh -c "$DISCORD_START" || true
            fi
        else
            sh -c "$DISCORD_START" || true
        fi
        sleep 2
    done
}

start_ssh() {
    [ "$SSH_ENABLE" = 1 ] || return 0
    mkdir -p /run/sshd
    ssh-keygen -A >/dev/null 2>&1 || true
    /usr/sbin/sshd -D &
}

run_windows_exe() {
    [ -f /efi/SteveOS/Server/run-exe.conf ] || return 0
    EXE_AUTORUN=$(sed -n 's/^EXE_AUTORUN=//p' /efi/SteveOS/Server/run-exe.conf 2>/dev/null | head -n1)
    case "$EXE_AUTORUN" in
        /efi/SteveOS/Apps/*.[eE][xX][eE])
            echo "Launching Windows EXE with Wine: $EXE_AUTORUN"
            /usr/local/bin/steveos-run-exe "$EXE_AUTORUN" &
            rm -f /efi/SteveOS/Server/run-exe.conf
            ;;
        "") ;;
        *) echo "Ignoring invalid Windows EXE path" ;;
    esac
}

start_tailscale
start_ssh
start_discord &

echo "SteveOS Server Mode"
ip -brief addr 2>/dev/null || true
echo "Discord: $DISCORD_ENABLE"
echo "Tailscale: $TAILSCALE_ENABLE"
echo "SSH: $SSH_ENABLE"
echo "Windows EXE runtime: Wine + Xvfb"

exec /bin/sh
EOF

chmod +x $ROOT/init
cp $ROOT/boot/vmlinuz-virt $WORK/vmlinuz-virt

(
  cd $ROOT
  find . -xdev -print0 | cpio --null -o -H newc --quiet
) | gzip -9 > $WORK/server-initramfs.img

cat > $WORK/grub.cfg <<'EOF'
set timeout=2
set default=0
search --file --set=root /SteveOS/Server/server-initramfs.img
menuentry "SteveOS Server Mode" {
    linux /SteveOS/Server/vmlinuz-virt
    initrd /SteveOS/Server/server-initramfs.img
}
EOF

grub-mkstandalone -O x86_64-efi -o $WORK/ServerBoot.efi \
  "boot/grub/grub.cfg=$WORK/grub.cfg"

echo "Server runtime built in $WORK"
