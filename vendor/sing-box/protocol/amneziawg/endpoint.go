// Utgard integration; GPL-3.0-or-later.
package amneziawg

import (
	"context"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"net"
	"net/netip"
	"strings"
	"sync"

	"github.com/amnezia-vpn/amneziawg-go/device"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/adapter/endpoint"
	"github.com/sagernet/sing-box/common/dialer"
	"github.com/sagernet/sing-box/common/iponly"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/bufio"
	E "github.com/sagernet/sing/common/exceptions"
	M "github.com/sagernet/sing/common/metadata"
	N "github.com/sagernet/sing/common/network"
	"github.com/sagernet/sing/service"
)

type Options struct {
	option.WireGuardEndpointOptions
	Amnezia string `json:"amnezia,omitempty"`
}

func RegisterEndpoint(registry *endpoint.Registry) {
	endpoint.Register[Options](registry, "amneziawg", NewEndpoint)
}

type Endpoint struct {
	endpoint.Adapter
	ctx     context.Context
	logger  log.ContextLogger
	options Options
	dns     adapter.DNSRouter
	dialer  N.Dialer
	stack   *stackDevice
	device  *device.Device
	ipc     string
	mu      sync.Mutex
	closed  bool
}

var _ adapter.InterfaceUpdateListener = (*Endpoint)(nil)

func keyHex(value string) (string, error) {
	bytes, err := base64.StdEncoding.DecodeString(value)
	if err != nil || len(bytes) != 32 {
		return "", E.New("invalid AmneziaWG key")
	}
	return hex.EncodeToString(bytes), nil
}

func NewEndpoint(ctx context.Context, router adapter.Router, logger log.ContextLogger, tag string, options Options) (adapter.Endpoint, error) {
	if options.System || options.ListenPort != 0 || len(options.Peers) != 1 {
		return nil, E.New("AmneziaWG requires a userspace endpoint with exactly one peer and no listen_port")
	}
	if len(options.Peers[0].Reserved) != 0 {
		return nil, E.New("AmneziaWG does not support reserved")
	}
	if len(options.Address) == 0 {
		return nil, E.New("missing AmneziaWG address")
	}
	if options.MTU == 0 {
		options.MTU = 1280
	}
	if options.MTU < 1280 || options.MTU > 1500 {
		return nil, E.New("AmneziaWG MTU must be 1280..1500")
	}
	parameters, err := validateParameters(options.Amnezia, options.MTU)
	if err != nil {
		return nil, err
	}
	privateKey, err := keyHex(options.PrivateKey)
	if err != nil {
		return nil, err
	}
	peer := options.Peers[0]
	publicKey, err := keyHex(peer.PublicKey)
	if err != nil {
		return nil, err
	}
	ipc := "private_key=" + privateKey + "\n" + parameters + "public_key=" + publicKey + "\n"
	if peer.PreSharedKey != "" {
		key, err := keyHex(peer.PreSharedKey)
		if err != nil {
			return nil, err
		}
		ipc += "preshared_key=" + key + "\n"
	}
	if len(peer.AllowedIPs) == 0 {
		return nil, E.New("missing AmneziaWG allowed_ips")
	}
	for _, prefix := range peer.AllowedIPs {
		ipc += "allowed_ip=" + prefix.String() + "\n"
	}
	ipc += fmt.Sprintf("persistent_keepalive_interval=%d\n", peer.PersistentKeepaliveInterval)
	destination := M.ParseSocksaddrHostPort(peer.Address, peer.Port)
	if !destination.IsValid() || peer.Port == 0 {
		return nil, E.New("invalid AmneziaWG peer address")
	}
	outboundDialer, err := dialer.NewWithOptions(dialer.Options{
		Context: ctx, Options: options.DialerOptions, RemoteIsDomain: destination.IsDomain(), ResolverOnDetour: true,
	})
	if err != nil {
		return nil, err
	}
	stack, err := newStackDevice(DeviceOptions{Context: ctx, Logger: logger, MTU: options.MTU, Address: options.Address})
	if err != nil {
		return nil, err
	}
	return &Endpoint{
		Adapter: endpoint.NewAdapterWithDialerOptions("amneziawg", tag, []string{N.NetworkTCP, N.NetworkUDP}, options.DialerOptions),
		ctx:     ctx, logger: logger, options: options, dns: service.FromContext[adapter.DNSRouter](ctx),
		dialer: outboundDialer, stack: stack, ipc: ipc,
	}, nil
}

func (e *Endpoint) Start(stage adapter.StartStage) error {
	if stage != adapter.StartStatePostStart {
		return nil
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.closed {
		return net.ErrClosed
	}
	peer := e.options.Peers[0]
	destination := M.ParseSocksaddrHostPort(peer.Address, peer.Port)
	address := destination.Addr
	if destination.IsDomain() {
		addresses, err := e.dns.Lookup(e.ctx, destination.Fqdn, e.dialer.(dialer.ResolveDialer).QueryOptions())
		if err != nil {
			return err
		}
		if len(addresses) == 0 {
			return E.New("no addresses for AmneziaWG server")
		}
		address = addresses[0]
	}
	remote := netip.AddrPortFrom(address, peer.Port)
	bind := &clientBind{ctx: e.ctx, dialer: e.dialer, destination: destination, endpoint: remoteEndpoint(remote)}
	logger := &device.Logger{
		Verbosef: func(format string, args ...any) { e.logger.Debug(fmt.Sprintf(format, args...)) },
		Errorf:   func(format string, args ...any) { e.logger.Error(fmt.Sprintf(format, args...)) },
	}
	wg := device.NewDevice(e.stack, bind, logger)
	if err := wg.IpcSet(e.ipc + "endpoint=" + remote.String() + "\n"); err != nil {
		wg.Close()
		// Upstream UAPI errors may include values. Never log the configuration.
		return E.New("invalid AmneziaWG device configuration")
	}
	if err := wg.Up(); err != nil {
		wg.Close()
		return err
	}
	e.device = wg
	e.ipc = ""
	e.options.PrivateKey = ""
	e.options.Peers[0].PreSharedKey = ""
	return nil
}

func (e *Endpoint) Close() error {
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.closed {
		return nil
	}
	e.closed = true
	e.ipc = ""
	if e.device != nil {
		e.device.Close()
		return nil
	}
	return e.stack.Close()
}

func (e *Endpoint) InterfaceUpdated(ctx context.Context) {
	go func() {
		e.mu.Lock()
		defer e.mu.Unlock()
		if ctx.Err() == nil && !e.closed && e.device != nil {
			if err := e.device.BindUpdate(); err != nil {
				e.logger.Error("AmneziaWG update bind: ", err)
			}
		}
	}()
}

func (e *Endpoint) DialContext(ctx context.Context, network string, destination M.Socksaddr) (net.Conn, error) {
	if destination.IsDomain() {
		addresses, err := e.dns.Lookup(ctx, destination.Fqdn, adapter.DNSQueryOptions{})
		if err != nil {
			return nil, err
		}
		return N.DialSerial(ctx, e.stack, network, destination, addresses)
	}
	return e.stack.DialContext(ctx, network, destination)
}

func (e *Endpoint) ListenPacket(ctx context.Context, destination M.Socksaddr) (net.PacketConn, error) {
	if destination.IsDomain() {
		addresses, err := e.dns.Lookup(ctx, destination.Fqdn, adapter.DNSQueryOptions{})
		if err != nil {
			return nil, err
		}
		packetConn, address, err := N.ListenSerial(ctx, e.stack, destination, addresses)
		if err != nil {
			return nil, err
		}
		packetConn = iponly.NewPacketConn(e.logger, packetConn)
		return bufio.NewNATPacketConn(bufio.NewPacketConn(packetConn), M.SocksaddrFrom(address, destination.Port), destination), nil
	}
	packetConn, err := e.stack.ListenPacket(ctx, destination)
	if err != nil {
		return nil, err
	}
	return iponly.NewPacketConn(e.logger, packetConn), nil
}

// Keep UAPI parsing confined to validated device fields. Peer/key injection
// would otherwise be possible through a manually edited base configuration.
func splitParameter(line string) (string, string, error) {
	key, value, ok := strings.Cut(line, "=")
	if !ok || value == "" || strings.ContainsAny(value, "\r\x00") {
		return "", "", E.New("invalid AmneziaWG parameter")
	}
	return key, value, nil
}
