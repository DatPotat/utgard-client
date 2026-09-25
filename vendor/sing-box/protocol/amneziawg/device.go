// Utgard adaptation of sing-box v1.14.1; GPL-3.0-or-later.
package amneziawg

import (
	"context"
	"net/netip"
	"time"

	"github.com/amnezia-vpn/amneziawg-go/device"
	wgTun "github.com/amnezia-vpn/amneziawg-go/tun"
	"github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/control"
	"github.com/sagernet/sing/common/logger"
	N "github.com/sagernet/sing/common/network"
)

type Device interface {
	wgTun.Device
	N.Dialer
	Start() error
	SetDevice(device *device.Device)
	Inet4Address() netip.Addr
	Inet6Address() netip.Addr
}

type DeviceOptions struct {
	Context         context.Context
	Logger          logger.ContextLogger
	System          bool
	Handler         tun.Handler
	UDPTimeout      time.Duration
	ICMPTimeout     time.Duration
	UDPMapping      tun.NATMapping
	UDPFiltering    tun.NATFiltering
	UDPNATMax       uint32
	NetworkMonitor  tun.NetworkUpdateMonitor
	InterfaceFinder control.InterfaceFinder
	CreateDialer    func(interfaceName string) N.Dialer
	Name            string
	MTU             uint32
	Address         []netip.Prefix
	AllowedAddress  []netip.Prefix
}
