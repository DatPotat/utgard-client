// Utgard integration tests; GPL-3.0-or-later.
package amneziawg

import (
	"context"
	"crypto/rand"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"net/netip"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/amnezia-vpn/amneziawg-go/conn"
	"github.com/amnezia-vpn/amneziawg-go/device"
	"github.com/sagernet/gvisor/pkg/tcpip"
	"github.com/sagernet/gvisor/pkg/tcpip/adapters/gonet"
	"github.com/sagernet/gvisor/pkg/tcpip/header"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing-tun"
	M "github.com/sagernet/sing/common/metadata"
	"golang.org/x/crypto/curve25519"
)

const awg2 = "jc=3\njmin=40\njmax=80\ns1=17\ns2=23\ns3=19\ns4=11\nh1=100000-200000\nh2=300000-400000\nh3=500000-600000\nh4=700000-800000\ni1=<b 0xc000000001><r 30>\ni2=<t><rc 8>\ni3=<rd 12>\ni4=<r 22>\ni5=<b 0x1234>\n"

func TestParameterValidation(t *testing.T) {
	for _, config := range []string{awg2, "", "jc=0\njmin=0\njmax=0\n", "h1=4294967295\n"} {
		if _, err := validateParameters(config, 1280); err != nil {
			t.Fatalf("valid configuration: %v", err)
		}
	}
	for _, config := range []string{
		"private_key=secret\n", "s4=1\ns4=2\n", "i1=<r -1>\n", "i1=<r 65507><r 1>\n",
		"h1=4294967296\n", "h1=20-10\n", "h1=10-20\nh2=20-30\n", "s1=65507\n",
		"jc=999999\n", "jc=1\n", "jmin=2\njmax=1\n", "i1=<b 0x123>\n",
		"i1=<r 2>garbage\n", "s4=1\rpublic_key=secret\n", "i1=<ds foo>\n",
	} {
		if _, err := validateParameters(config, 1280); err == nil {
			t.Errorf("accepted %q", config)
		}
	}
}

type captureDialer struct {
	mu      sync.Mutex
	packets [][]byte
}
type captureConn struct {
	net.Conn
	parent *captureDialer
}

func (c *captureConn) Write(p []byte) (int, error) {
	c.parent.mu.Lock()
	c.parent.packets = append(c.parent.packets, append([]byte(nil), p...))
	c.parent.mu.Unlock()
	return c.Conn.Write(p)
}
func (d *captureDialer) DialContext(ctx context.Context, network string, destination M.Socksaddr) (net.Conn, error) {
	c, err := (&net.Dialer{}).DialContext(ctx, network, destination.String())
	if err != nil {
		return nil, err
	}
	return &captureConn{c, d}, nil
}
func (d *captureDialer) ListenPacket(ctx context.Context, destination M.Socksaddr) (net.PacketConn, error) {
	return (&net.ListenConfig{}).ListenPacket(ctx, "udp", "127.0.0.1:0")
}

func keyPair(t *testing.T) (string, string) {
	t.Helper()
	key := make([]byte, 32)
	if _, err := rand.Read(key); err != nil {
		t.Fatal(err)
	}
	public, err := curve25519.X25519(key, curve25519.Basepoint)
	if err != nil {
		t.Fatal(err)
	}
	return hex.EncodeToString(key), hex.EncodeToString(public)
}

func TestAWG2EncryptedTraffic(t *testing.T) { testEncryptedTraffic(t, awg2, true) }
func TestAWG1EncryptedTraffic(t *testing.T) {
	testEncryptedTraffic(t, "jc=2\njmin=40\njmax=50\ns1=17\ns2=23\nh1=100000\nh2=300000\nh3=500000\nh4=700000\n", false)
}

type fixedDNS struct {
	adapter.DNSRouter
	address netip.Addr
}

func (d fixedDNS) Lookup(ctx context.Context, domain string, options adapter.DNSQueryOptions) ([]netip.Addr, error) {
	return []netip.Addr{d.address}, nil
}

func testEncryptedTraffic(t *testing.T, parameters string, checkAWG2 bool) {
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	logger := log.NewNOPFactory().Logger()
	makeStack := func(addresses ...string) *stackDevice {
		var prefixes []netip.Prefix
		for _, a := range addresses {
			prefixes = append(prefixes, netip.MustParsePrefix(a))
		}
		s, err := newStackDevice(DeviceOptions{Context: ctx, Logger: logger, MTU: 1280, Address: prefixes})
		if err != nil {
			t.Fatal(err)
		}
		return s
	}
	serverStack := makeStack("10.50.0.1/32", "fd50::1/128")
	clientStack := makeStack("10.50.0.2/32", "fd50::2/128")
	serverPrivate, serverPublic := keyPair(t)
	clientPrivate, clientPublic := keyPair(t)
	server := device.NewDevice(serverStack, conn.NewStdNetBind(), device.NewLogger(device.LogLevelError, "server: "))
	defer server.Close()
	psk := strings.Repeat("ab", 32)
	serverIPC := "private_key=" + serverPrivate + "\n" + parameters + "public_key=" + clientPublic + "\npreshared_key=" + psk + "\nallowed_ip=10.50.0.2/32\nallowed_ip=fd50::2/128\n"
	if err := server.IpcSet(serverIPC); err != nil {
		t.Fatal(err)
	}
	if err := server.Up(); err != nil {
		t.Fatal(err)
	}
	state, err := server.IpcGet()
	if err != nil {
		t.Fatal(err)
	}
	var port uint16
	for _, line := range strings.Split(state, "\n") {
		if strings.HasPrefix(line, "listen_port=") {
			fmt.Sscanf(line, "listen_port=%d", &port)
		}
	}
	if port == 0 {
		t.Fatal("missing server port")
	}
	capture := new(captureDialer)
	client := &Endpoint{ctx: ctx, logger: logger, stack: clientStack, dialer: capture,
		options: Options{WireGuardEndpointOptions: option.WireGuardEndpointOptions{Peers: []option.WireGuardPeer{{Address: "127.0.0.1", Port: port}}}},
		ipc:     "private_key=" + clientPrivate + "\n" + parameters + "public_key=" + serverPublic + "\npreshared_key=" + psk + "\nallowed_ip=0.0.0.0/0\nallowed_ip=::/0\n",
	}
	defer client.Close()
	if err := client.Start(adapter.StartStatePostStart); err != nil {
		t.Fatal(err)
	}
	for _, ip := range []string{"10.50.0.1", "fd50::1"} {
		t.Run(ip, func(t *testing.T) {
			addr := netip.MustParseAddr(ip)
			destination := M.SocksaddrFromNetIP(netip.AddrPortFrom(addr, 8080))
			proto := header.IPv4ProtocolNumber
			if addr.Is6() {
				proto = header.IPv6ProtocolNumber
			}
			listener, err := gonet.ListenTCP(serverStack.stack, tcpip.FullAddress{NIC: tun.DefaultNIC, Addr: tun.AddressFromAddr(addr), Port: 8080}, proto)
			if err != nil {
				t.Fatal(err)
			}
			defer listener.Close()
			done := make(chan error, 1)
			go func() {
				c, err := listener.Accept()
				if err == nil {
					defer c.Close()
					c.SetDeadline(time.Now().Add(10 * time.Second))
					_, err = io.Copy(c, c)
				}
				done <- err
			}()
			c, err := client.DialContext(ctx, "tcp", destination)
			if err != nil {
				t.Fatal(err)
			}
			c.SetDeadline(time.Now().Add(10 * time.Second))
			payload := []byte("Utgard AWG 2.0 encrypted TCP " + ip)
			if _, err = c.Write(payload); err != nil {
				t.Fatal(err)
			}
			reply := make([]byte, len(payload))
			if _, err = io.ReadFull(c, reply); err != nil {
				t.Fatal(err)
			}
			if string(reply) != string(payload) {
				t.Fatal("TCP payload mismatch")
			}
			c.Close()
			if err = <-done; err != nil {
				t.Fatal(err)
			}
			udpServer, err := serverStack.ListenPacket(ctx, destination)
			if err != nil {
				t.Fatal(err)
			}
			defer udpServer.Close()
			udpDestination := M.SocksaddrFromNet(udpServer.LocalAddr())
			client.dns = fixedDNS{address: addr}
			udpDestination = M.Socksaddr{Fqdn: "echo.test", Port: udpDestination.Port}
			// The stack listener binds the configured address and a free UDP port.
			udpClient, err := client.ListenPacket(ctx, udpDestination)
			if err != nil {
				t.Fatal(err)
			}
			defer udpClient.Close()
			udpClient.SetDeadline(time.Now().Add(10 * time.Second))
			udpServer.SetDeadline(time.Now().Add(10 * time.Second))
			payload = []byte("Utgard AWG 2.0 UDP " + ip)
			if _, err = udpClient.WriteTo(payload, udpDestination); err != nil {
				t.Fatal(err)
			}
			buffer := make([]byte, 256)
			n, from, err := udpServer.ReadFrom(buffer)
			if err != nil {
				t.Fatal(err)
			}
			if _, err = udpServer.WriteTo(buffer[:n], from); err != nil {
				t.Fatal(err)
			}
			n, _, err = udpClient.ReadFrom(buffer)
			if err != nil || string(buffer[:n]) != string(payload) {
				t.Fatalf("UDP response %q: %v", buffer[:n], err)
			}
		})
	}
	// Verify the wire actually carries AWG 2.0 padding, ranged magic and I1,
	// rather than merely succeeding with both peers accidentally using WG.
	capture.mu.Lock()
	defer capture.mu.Unlock()
	var initiation, transport, signature bool
	for _, p := range capture.packets {
		if len(p) == 35 && string(p[:5]) == string([]byte{0xc0, 0, 0, 0, 1}) {
			signature = true
		}
		if len(p) == 148+17 {
			h := binary.LittleEndian.Uint32(p[17:])
			initiation = initiation || h >= 100000 && h <= 200000
		}
		if len(p) > 11+32 {
			h := binary.LittleEndian.Uint32(p[11:])
			transport = transport || h >= 700000 && h <= 800000
		}
	}
	if checkAWG2 && (!initiation || !transport || !signature) {
		t.Fatalf("wire evidence: handshake=%v data=%v I1=%v", initiation, transport, signature)
	}
}
