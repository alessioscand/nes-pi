import sys
# neo-pi: wired Xbox 360 compatible (XInput) pads of any brand.
# Circle recognises Xbox 360 pads only by Microsoft's vendor/product IDs.
# Third-party pads (and adapters that switch to "PC mode", like the Nacon /
# Bigben PS4 wireless receiver 146b:0d06 -> 146b:0609) are vendor-specific
# devices whose first interface is class ff, subclass 5d, protocol 01: give
# those to the Xbox 360 driver, as Linux xpad does.
# Usage: python3 patches/circle-xinput.py circle-rpi4
root=sys.argv[1]
def edit(path, a, b):
    p=f'{root}/{path}'; t=open(p).read()
    if b in t: return
    assert t.count(a)==1, (path, a[:60])
    open(p,'w').write(t.replace(a,b))
edit('lib/usb/usbdevice.cpp', '''			pChild = CUSBDeviceFactory::GetDevice (m_pFunction[nFunction], GetName (DeviceNameVendor));
			if (pChild == 0)
			{
				pChild = CUSBDeviceFactory::GetDevice (m_pFunction[nFunction], GetName (DeviceNameDevice));
			}
''', '''			pChild = CUSBDeviceFactory::GetDevice (m_pFunction[nFunction], GetName (DeviceNameVendor));
			if (pChild == 0)
			{
				pChild = CUSBDeviceFactory::GetDevice (m_pFunction[nFunction], GetName (DeviceNameDevice));
			}

			// neo-pi: third-party wired Xbox 360 (XInput) pad
			if (   pChild == 0
			    && pInterfaceDesc->bInterfaceClass == 0xFF
			    && pInterfaceDesc->bInterfaceSubClass == 0x5D
			    && pInterfaceDesc->bInterfaceProtocol == 0x01)
			{
				pChild = CUSBDeviceFactory::GetDevice (m_pFunction[nFunction], new CString ("xinput"));
			}
''')
edit('lib/usb/usbdevicefactory.cpp', '''	else if (pName->Compare ("ven57e-2009") == 0)''', '''	else if (pName->Compare ("xinput") == 0)	// neo-pi: see usbdevice.cpp
	{
		pResult = new CUSBGamePadXbox360Device (pParent);
	}
	else if (pName->Compare ("ven57e-2009") == 0)''')
# Xbox 360 reports are 20 bytes, but the driver asked for only 14: the xHCI
# controller (Pi 4) stops the transfer with a "babble" error and no report
# ever arrives. Ask for a full 32-byte packet and accept reports of 14+ bytes.
edit('lib/usb/usbgamepadxbox360.cpp', '''	m_usReportSize = REPORT_SIZE;
''', '''	m_usReportSize = 32;	// neo-pi: a full packet, reports are 20 bytes
''')
edit('lib/usb/usbgamepadxbox360.cpp', '''	    && nReportSize == REPORT_SIZE
''', '''	    && nReportSize >= REPORT_SIZE	// neo-pi: 20-byte reports
''')
print(root,'ok')
