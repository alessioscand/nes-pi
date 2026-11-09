import sys
root=sys.argv[1]
def edit(path, a, b):
    p=f'{root}/{path}'; t=open(p).read()
    if b in t: return
    assert t.count(a)==1, (path, a[:60])
    open(p,'w').write(t.replace(a,b))
# global knob
edit('include/circle/usb/usb.h', '''PACKED;

#endif''', '''PACKED;

// neo-pi: maximum polling interval (ms) for interrupt IN endpoints of
// low-/full-speed devices (like usbhid.jspoll on Linux). 0 = use bInterval.
extern unsigned g_nUSBInterruptPollMs;

#endif''')
# Pi 1-3 (DWHCI)
edit('lib/usb/usbendpoint.cpp', '''		if (m_pDevice->GetSpeed () < USBSpeedHigh)
		{
			m_nInterval = ucInterval;
		}''', '''		if (m_pDevice->GetSpeed () < USBSpeedHigh)
		{
			m_nInterval = ucInterval;

			// neo-pi: poll game controllers faster than they ask for
			if (   m_Type == EndpointTypeInterrupt && m_bDirectionIn
			    && m_pDevice->GetDeviceDescriptor () != 0
			    && m_pDevice->GetDeviceDescriptor ()->bDeviceClass != 9	// not a hub
			    && g_nUSBInterruptPollMs != 0 && m_nInterval > g_nUSBInterruptPollMs)
			{
				CLogger::Get ()->Write ("uep", LogNotice, "Interrupt polling %u -> %u ms",
							m_nInterval, g_nUSBInterruptPollMs);
				m_nInterval = g_nUSBInterruptPollMs;
			}
		}''')
edit('lib/usb/usbendpoint.cpp', '''CUSBEndpoint::CUSBEndpoint (CUSBDevice *pDevice)\n''', '''unsigned g_nUSBInterruptPollMs = 1;

CUSBEndpoint::CUSBEndpoint (CUSBDevice *pDevice)\n''')
# Pi 4/5 (xHCI)
edit('lib/usb/xhciendpoint.cpp', '''		m_uchInterval = ConvertInterval (pDesc->bInterval, m_pDevice->GetSpeed ());
	}
	else
	{
		m_uchInterval = 0;
	}''', '''		u8 uchInterval = pDesc->bInterval;

		// neo-pi: poll game controllers faster than they ask for
		if (   (m_uchAttributes & 3) == 3 && (m_uchEndpointAddress & 0x80)
		    && m_pDevice->GetSpeed () < USBSpeedHigh
		    && m_pDevice->GetDeviceDescriptor () != 0
		    && m_pDevice->GetDeviceDescriptor ()->bDeviceClass != 9	// not a hub
		    && g_nUSBInterruptPollMs != 0 && uchInterval > g_nUSBInterruptPollMs)
		{
			CLogger::Get ()->Write ("xhciep", LogNotice, "Interrupt polling %u -> %u ms",
						(unsigned) uchInterval, g_nUSBInterruptPollMs);
			uchInterval = (u8) g_nUSBInterruptPollMs;
		}

		m_uchInterval = ConvertInterval (uchInterval, m_pDevice->GetSpeed ());
	}
	else
	{
		m_uchInterval = 0;
	}''')
print(root,'ok')
