"""UFI composite interface 2. Never reset/configure the shared GW interface."""
class BulkStream:
    def __init__(self, device, usb_util, timeout=200):
        self.device, self.util, self.timeout = device, usb_util, timeout
        self.pending = bytearray()
        config = device.get_active_configuration()
        interface = config[(2, 0)]
        endpoints = {ep.bEndpointAddress: ep for ep in interface}
        if interface.bInterfaceClass != 0xff or not {0x03, 0x83} <= endpoints.keys():
            raise ValueError("device has no UFI native bulk interface")
        usb_util.claim_interface(device, 2)

    def write(self, data):
        return self.device.write(0x03, data, timeout=5000)

    def read(self, size):
        if not self.pending:
            try:
                self.pending.extend(self.device.read(0x83, ((size+63)//64)*64,
                                                     timeout=self.timeout))
            except Exception as exc:
                if getattr(exc, "errno", None) == 110 or getattr(exc, "backend_error_code", None) == -7:
                    return b""
                raise
        result = bytes(self.pending[:size])
        del self.pending[:size]
        return result

    def reset_input_buffer(self):
        # Interface-local parser reset; never SET_CONFIGURATION or USB device reset.
        self.device.ctrl_transfer(0x41, 0x21, 0, 2, b"", timeout=1000)
        # Firmware restarts only native IN. Synchronize its host/device DATA toggle.
        self.device.clear_halt(0x83)
        self.pending.clear()

    def close(self):
        self.util.release_interface(self.device, 2)
        self.util.dispose_resources(self.device)

    def cancel(self):
        self.device.ctrl_transfer(0x41, 0x22, 0, 2, b"", timeout=1000)


def open_bulk(serial=None):
    try:
        import usb.core
        import usb.util
        import usb.backend.libusb1
    except ImportError as exc:
        raise RuntimeError("Composite USB requires the host package's [usb] extra") from exc
    backend = None
    try:
        import libusb_package
        backend = usb.backend.libusb1.get_backend(find_library=libusb_package.find_library)
    except ImportError:
        pass
    devices = list(usb.core.find(find_all=True, idVendor=0x1209, idProduct=0x4f58,
                                backend=backend))
    if serial:
        devices = [d for d in devices if d.serial_number == serial]
    if len(devices) != 1:
        raise RuntimeError(f"Expected one UFI composite device, found {len(devices)}; use usb:SERIAL")
    return BulkStream(devices[0], usb.util)
