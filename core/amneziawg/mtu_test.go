package amneziawg

import (
	"bytes"
	"context"
	"encoding/base64"
	"net"
	"net/netip"
	"os"
	"strconv"
	"testing"
	"time"

	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	M "github.com/sagernet/sing/common/metadata"
)

// Unlike the encrypted-traffic tests, this exercises the production sing-box
// dialer. Run with UTGARD_TEST_MTU=1300 on an isolated container whose loopback
// MTU is 1300 to reproduce WSASend/EMSGSIZE for a 1280-byte AWG tunnel with S4=13.
func TestAWGOuterPacketMTU(t *testing.T) {
	limited := os.Getenv("UTGARD_TEST_MTU")
	if limited != "" {
		want, err := strconv.Atoi(limited)
		iface, ifaceErr := net.InterfaceByName("lo")
		if err != nil || ifaceErr != nil || iface.MTU != want || want != 1300 {
			t.Fatal("regression test requires isolated loopback MTU 1300")
		}
	}
	for _, mode := range []string{"default", "enabled", "disabled"} {
		t.Run(mode, func(t *testing.T) {
			if mode == "disabled" && limited == "" {
				t.Skip("requires constrained loopback")
			}
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			server, err := net.ListenPacket("udp4", "127.0.0.1:0")
			if err != nil {
				t.Fatal(err)
			}
			defer server.Close()
			key := base64.StdEncoding.EncodeToString(bytes.Repeat([]byte{1}, 32))
			options := Options{WireGuardEndpointOptions: option.WireGuardEndpointOptions{
				Address:    []netip.Prefix{netip.MustParsePrefix("10.50.0.2/32")},
				PrivateKey: key, MTU: 1280,
				Peers: []option.WireGuardPeer{{Address: "127.0.0.1", Port: M.SocksaddrFromNet(server.LocalAddr()).Port,
					PublicKey: key, AllowedIPs: []netip.Prefix{netip.MustParsePrefix("0.0.0.0/0")}}},
			}, Amnezia: "s4=13\n"}
			if mode != "default" {
				enabled := mode == "enabled"
				options.UDPFragment = &enabled
			}
			ep, err := NewEndpoint(ctx, nil, log.NewNOPFactory().Logger(), "mtu-test", options)
			if err != nil {
				t.Fatal(err)
			}
			defer ep.Close()
			client, err := ep.(*Endpoint).dialer.DialContext(ctx, "udp", M.SocksaddrFromNet(server.LocalAddr()))
			if err != nil {
				t.Fatal(err)
			}
			defer client.Close()
			// 1280 inner IP + 32 WG overhead + 13 S4; outer IPv4/UDP adds 28.
			payload := bytes.Repeat([]byte{0xa5}, 1280+32+13)
			_, err = client.Write(payload)
			if mode == "disabled" {
				if err == nil {
					t.Fatal("DF socket unexpectedly sent oversized packet")
				}
				t.Log("expected rejection with explicit udp_fragment=false:", err)
				return
			}
			if err != nil {
				t.Fatalf("AWG packet rejected: %v", err)
			}
			server.SetReadDeadline(time.Now().Add(3 * time.Second))
			buffer := make([]byte, 2048)
			n, _, err := server.ReadFrom(buffer)
			if err != nil || !bytes.Equal(buffer[:n], payload) {
				t.Fatalf("fragmented AWG packet did not reassemble: bytes=%d, err=%v", n, err)
			}
		})
	}
}
