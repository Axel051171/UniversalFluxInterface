#!/bin/bash
# ============================================================================
# UFI CM5 Setup Script
# 
# Installiert die UFI Processing Software auf Raspberry Pi CM5
# ============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}"
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║           UFI CM5 Processing Layer Setup                      ║"
echo "║           Universal Flux Interface                            ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

# ============================================================================
# System-Check
# ============================================================================

echo -e "${YELLOW}[1/8] System-Check...${NC}"

# Root-Check
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Bitte als root ausführen: sudo $0${NC}"
    exit 1
fi

# Raspberry Pi Check
if ! grep -q "Raspberry Pi" /proc/cpuinfo 2>/dev/null; then
    echo -e "${YELLOW}Warnung: Kein Raspberry Pi erkannt. Fortfahren? (j/n)${NC}"
    read -r answer
    if [ "$answer" != "j" ]; then
        exit 1
    fi
fi

# ============================================================================
# System-Update
# ============================================================================

echo -e "${YELLOW}[2/8] System-Update...${NC}"

apt-get update
apt-get upgrade -y

# ============================================================================
# Dependencies installieren
# ============================================================================

echo -e "${YELLOW}[3/8] Dependencies installieren...${NC}"

apt-get install -y \
    python3 \
    python3-pip \
    python3-venv \
    python3-numpy \
    python3-aiohttp \
    python3-usb \
    libusb-1.0-0 \
    libusb-1.0-0-dev \
    nginx \
    git \
    htop \
    screen \
    usbutils

# Python Packages
pip3 install --break-system-packages \
    pyusb \
    aiohttp \
    aiohttp-cors \
    numpy

# ============================================================================
# UFI Benutzer erstellen
# ============================================================================

echo -e "${YELLOW}[4/8] UFI Benutzer konfigurieren...${NC}"

# Benutzer erstellen falls nicht vorhanden
if ! id "ufi" &>/dev/null; then
    useradd -m -s /bin/bash ufi
    echo "ufi:ufi" | chpasswd
fi

# Zu USB-Gruppe hinzufügen
usermod -a -G dialout,plugdev ufi

# ============================================================================
# UFI Software installieren
# ============================================================================

echo -e "${YELLOW}[5/8] UFI Software installieren...${NC}"

UFI_DIR="/opt/ufi"
mkdir -p $UFI_DIR
mkdir -p $UFI_DIR/logs
mkdir -p $UFI_DIR/data
mkdir -p $UFI_DIR/cache

# Software kopieren
cp -r software/cm5/* $UFI_DIR/
cp -r software/web $UFI_DIR/

# Berechtigungen setzen
chown -R ufi:ufi $UFI_DIR
chmod +x $UFI_DIR/*.py

# ============================================================================
# USB Rules für STM32
# ============================================================================

echo -e "${YELLOW}[6/8] USB Rules konfigurieren...${NC}"

cat > /etc/udev/rules.d/99-ufi.rules << 'EOF'
# UFI Flux Engine (STM32H723)
SUBSYSTEM=="usb", ATTR{idVendor}=="1209", ATTR{idProduct}=="4f54", MODE="0666", GROUP="plugdev", SYMLINK+="ufi%n"

# STM32 DFU Mode
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", ATTR{idProduct}=="df11", MODE="0666", GROUP="plugdev"

# Generic STM32 VCP
SUBSYSTEM=="tty", ATTRS{idVendor}=="0483", MODE="0666", GROUP="dialout"
EOF

udevadm control --reload-rules
udevadm trigger

# ============================================================================
# Systemd Service
# ============================================================================

echo -e "${YELLOW}[7/8] Systemd Service einrichten...${NC}"

cat > /etc/systemd/system/ufi-processor.service << 'EOF'
[Unit]
Description=UFI Processing Layer
After=network.target

[Service]
Type=simple
User=ufi
Group=ufi
WorkingDirectory=/opt/ufi
ExecStart=/usr/bin/python3 /opt/ufi/ufi_processor.py
Restart=always
RestartSec=5
StandardOutput=append:/opt/ufi/logs/processor.log
StandardError=append:/opt/ufi/logs/processor.log

# Umgebungsvariablen
Environment=PYTHONUNBUFFERED=1
Environment=UFI_DATA_DIR=/opt/ufi/data
Environment=UFI_CACHE_DIR=/opt/ufi/cache

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable ufi-processor.service

# ============================================================================
# Nginx Konfiguration
# ============================================================================

echo -e "${YELLOW}[8/8] Nginx konfigurieren...${NC}"

cat > /etc/nginx/sites-available/ufi << 'EOF'
server {
    listen 80 default_server;
    listen [::]:80 default_server;
    
    server_name ufi.local _;
    
    # API Proxy zu Python Backend
    location /api/ {
        proxy_pass http://127.0.0.1:5000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 300s;
        proxy_send_timeout 300s;
    }
    
    # WebSocket für Echtzeit-Updates
    location /ws {
        proxy_pass http://127.0.0.1:5000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400s;
    }
    
    # Statische Web-UI
    location / {
        root /opt/ufi/web/static;
        index index.html;
        try_files $uri $uri/ /index.html;
    }
    
    # Status Page
    location /status {
        stub_status on;
        allow 127.0.0.1;
        allow 192.168.0.0/16;
        allow 10.0.0.0/8;
        deny all;
    }
}
EOF

# Aktivieren
ln -sf /etc/nginx/sites-available/ufi /etc/nginx/sites-enabled/
rm -f /etc/nginx/sites-enabled/default

nginx -t && systemctl restart nginx

# ============================================================================
# Hostname setzen
# ============================================================================

echo "ufi" > /etc/hostname
sed -i 's/127.0.1.1.*/127.0.1.1\tufi.local\tufi/' /etc/hosts

# ============================================================================
# NVMe Mount (falls vorhanden)
# ============================================================================

if [ -b /dev/nvme0n1 ]; then
    echo -e "${YELLOW}NVMe gefunden, konfiguriere Mount...${NC}"
    
    # Partition prüfen/erstellen
    if [ ! -b /dev/nvme0n1p1 ]; then
        parted -s /dev/nvme0n1 mklabel gpt
        parted -s /dev/nvme0n1 mkpart primary ext4 0% 100%
        mkfs.ext4 -L UFI_DATA /dev/nvme0n1p1
    fi
    
    # Mount-Punkt
    mkdir -p /mnt/nvme
    
    # fstab Eintrag
    if ! grep -q "nvme0n1p1" /etc/fstab; then
        echo "/dev/nvme0n1p1 /mnt/nvme ext4 defaults,noatime 0 2" >> /etc/fstab
    fi
    
    mount -a
    
    # Symlinks für Daten
    rm -rf /opt/ufi/data /opt/ufi/cache
    mkdir -p /mnt/nvme/ufi/data /mnt/nvme/ufi/cache
    ln -s /mnt/nvme/ufi/data /opt/ufi/data
    ln -s /mnt/nvme/ufi/cache /opt/ufi/cache
    chown -R ufi:ufi /mnt/nvme/ufi
fi

# ============================================================================
# Fertig
# ============================================================================

echo -e "${GREEN}"
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║              UFI CM5 Setup abgeschlossen!                     ║"
echo "╠═══════════════════════════════════════════════════════════════╣"
echo "║                                                               ║"
echo "║   Web-Interface:    http://ufi.local                          ║"
echo "║   API:              http://ufi.local/api/                     ║"
echo "║   SSH:              ssh ufi@ufi.local (Passwort: ufi)        ║"
echo "║                                                               ║"
echo "║   Service starten:  sudo systemctl start ufi-processor       ║"
echo "║   Service Status:   sudo systemctl status ufi-processor      ║"
echo "║   Logs:             tail -f /opt/ufi/logs/processor.log      ║"
echo "║                                                               ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

echo -e "${YELLOW}System wird in 5 Sekunden neu gestartet...${NC}"
sleep 5
reboot
