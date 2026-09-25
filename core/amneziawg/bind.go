// Utgard integration; GPL-3.0-or-later.
package amneziawg

import (
	"context"
	"fmt"
	"net"
	"net/netip"
	"sync"

	"github.com/amnezia-vpn/amneziawg-go/v3/conn"
	M "github.com/sagernet/sing/common/metadata"
	N "github.com/sagernet/sing/common/network"
)

// A connected, single-peer bind uses sing-box's dialer so the UDP socket is
// excluded from the TUN routes. Never clear WireGuard's reserved bytes: AWG
// uses all four bytes for its randomized headers (and prepends padding).
type clientBind struct {
	ctx         context.Context
	dialer      N.Dialer
	destination M.Socksaddr
	endpoint    remoteEndpoint
	mu          sync.Mutex
	socket      net.Conn
}

var _ conn.Bind = (*clientBind)(nil)

func (b *clientBind) Open(port uint16) ([]conn.ReceiveFunc, uint16, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.socket != nil {
		return nil, 0, conn.ErrBindAlreadyOpen
	}
	socket, err := b.dialer.DialContext(b.ctx, N.NetworkUDP, b.destination)
	if err != nil {
		return nil, 0, err
	}
	b.socket = socket
	receive := func(packets [][]byte, sizes []int, eps []conn.Endpoint) (int, error) {
		n, err := socket.Read(packets[0])
		if err != nil {
			return 0, err
		}
		sizes[0], eps[0] = n, b.endpoint
		return 1, nil
	}
	return []conn.ReceiveFunc{receive}, M.SocksaddrFromNet(socket.LocalAddr()).Port, nil
}

func (b *clientBind) Close() error {
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.socket == nil {
		return nil
	}
	err := b.socket.Close()
	b.socket = nil
	return err
}

func (b *clientBind) Send(packets [][]byte, ep conn.Endpoint) error {
	b.mu.Lock()
	socket := b.socket
	b.mu.Unlock()
	if socket == nil {
		return net.ErrClosed
	}
	for _, packet := range packets {
		if _, err := socket.Write(packet); err != nil {
			return fmt.Errorf("send AWG UDP datagram (%d bytes): %w", len(packet), err)
		}
	}
	return nil
}
func (b *clientBind) SetMark(uint32) error { return nil }
func (b *clientBind) BatchSize() int       { return 1 }
func (b *clientBind) ParseEndpoint(s string) (conn.Endpoint, error) {
	addr, err := netip.ParseAddrPort(s)
	return remoteEndpoint(addr), err
}

type remoteEndpoint netip.AddrPort

func (e remoteEndpoint) ClearSrc()           {}
func (e remoteEndpoint) SrcToString() string { return "" }
func (e remoteEndpoint) DstToString() string { return netip.AddrPort(e).String() }
func (e remoteEndpoint) DstToBytes() []byte  { b, _ := netip.AddrPort(e).MarshalBinary(); return b }
func (e remoteEndpoint) DstIP() netip.Addr   { return netip.AddrPort(e).Addr() }
func (e remoteEndpoint) SrcIP() netip.Addr   { return netip.Addr{} }
