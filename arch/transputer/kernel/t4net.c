/*
 * T4Net — Simple MMIO Ethernet driver for Transputer T4 emulator
 *
 * Talks to the T4 emulator's built-in NIC at 0x30000000.
 * The emulator bridges packets to a host TAP device for real networking.
 *
 * Register map:
 *   0x00  STATUS    (R)   bit0=link_up, bit1=rx_ready, bit2=tx_ready
 *   0x04  TX_LEN    (W)   write packet length → triggers TX
 *   0x08  RX_LEN    (R)   length of pending RX packet (0 = none)
 *   0x0C  MAC_LO    (R)   MAC bytes 0-3
 *   0x10  MAC_HI    (R)   MAC bytes 4-5
 *   0x14  RX_ACK    (W)   acknowledge RX packet consumed
 *   0x1000  TX_BUF  (W)   TX packet buffer (1536 bytes)
 *   0x2000  RX_BUF  (R)   RX packet buffer (1536 bytes)
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/init.h>

#define T4NET_BASE       0x30000000UL
#define T4NET_REG_STATUS 0x00
#define T4NET_REG_TX_LEN 0x04
#define T4NET_REG_RX_LEN 0x08
#define T4NET_REG_MAC_LO 0x0C
#define T4NET_REG_MAC_HI 0x10
#define T4NET_REG_RX_ACK 0x14
#define T4NET_TX_BUF     0x1000
#define T4NET_RX_BUF     0x2000
#define T4NET_BUF_SIZE   1536

#define T4NET_STATUS_LINK_UP  0x01
#define T4NET_STATUS_RX_READY 0x02
#define T4NET_STATUS_TX_READY 0x04

/* MMIO access — use volatile pointers (no ioremap on NOMMU transputer) */
static inline unsigned int t4net_readl(unsigned int offset)
{
	return *(volatile unsigned int *)(T4NET_BASE + offset);
}

static inline void t4net_writel(unsigned int val, unsigned int offset)
{
	*(volatile unsigned int *)(T4NET_BASE + offset) = val;
}

static inline unsigned char t4net_readb(unsigned int offset)
{
	return *(volatile unsigned char *)(T4NET_BASE + offset);
}

static inline void t4net_writeb(unsigned char val, unsigned int offset)
{
	*(volatile unsigned char *)(T4NET_BASE + offset) = val;
}

struct t4net_priv {
	struct net_device *dev;
	struct timer_list poll_timer;
	struct net_device_stats stats;
};

/* RX poll — called from timer to check for incoming packets */
static void t4net_rx_poll(struct net_device *dev)
{
	struct t4net_priv *priv = netdev_priv(dev);
	unsigned int rx_len;
	struct sk_buff *skb;
	unsigned int i;
	unsigned char *data;

	rx_len = t4net_readl(T4NET_REG_RX_LEN);
	if (rx_len == 0 || rx_len > T4NET_BUF_SIZE)
		return;

	skb = dev_alloc_skb(rx_len + 2);
	if (!skb) {
		priv->stats.rx_dropped++;
		t4net_writel(1, T4NET_REG_RX_ACK);
		return;
	}

	skb_reserve(skb, 2);  /* align IP header */
	data = skb_put(skb, rx_len);

	/* Read packet from RX buffer byte-by-byte */
	for (i = 0; i < rx_len; i++)
		data[i] = t4net_readb(T4NET_RX_BUF + i);

	/* Acknowledge — tell emulator we consumed the packet */
	t4net_writel(1, T4NET_REG_RX_ACK);

	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;

	netif_rx(skb);
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += rx_len;
}

static void t4net_poll_timer_fn(struct timer_list *timer)
{
	struct t4net_priv *priv = timer_container_of(priv, timer, poll_timer);
	struct net_device *dev = priv->dev;

	t4net_rx_poll(dev);

	/* Re-arm timer — poll every 10ms (HZ/100) */
	mod_timer(&priv->poll_timer, jiffies + (HZ / 100));
}

static int t4net_open(struct net_device *dev)
{
	struct t4net_priv *priv = netdev_priv(dev);

	/* Start RX poll timer */
	timer_setup(&priv->poll_timer, t4net_poll_timer_fn, 0);
	mod_timer(&priv->poll_timer, jiffies + HZ);

	netif_start_queue(dev);
	printk("t4net: interface up\n");
	return 0;
}

static int t4net_stop(struct net_device *dev)
{
	struct t4net_priv *priv = netdev_priv(dev);

	timer_delete_sync(&priv->poll_timer);
	netif_stop_queue(dev);
	printk("t4net: interface down\n");
	return 0;
}

static netdev_tx_t t4net_start_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	struct t4net_priv *priv = netdev_priv(dev);
	unsigned int len = skb->len;
	unsigned char *data = skb->data;
	unsigned int i;

	if (len > T4NET_BUF_SIZE) {
		dev_kfree_skb(skb);
		priv->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	/* Copy packet to TX buffer byte-by-byte */
	for (i = 0; i < len; i++)
		t4net_writeb(data[i], T4NET_TX_BUF + i);

	/* Write TX length — triggers the send in the emulator */
	t4net_writel(len, T4NET_REG_TX_LEN);

	priv->stats.tx_packets++;
	priv->stats.tx_bytes += len;

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static struct net_device_stats *t4net_get_stats(struct net_device *dev)
{
	struct t4net_priv *priv = netdev_priv(dev);
	return &priv->stats;
}

static const struct net_device_ops t4net_netdev_ops = {
	.ndo_open	= t4net_open,
	.ndo_stop	= t4net_stop,
	.ndo_start_xmit	= t4net_start_xmit,
	.ndo_get_stats	= t4net_get_stats,
};

static struct net_device *t4net_dev;

static int __init t4net_init(void)
{
	struct net_device *dev;
	struct t4net_priv *priv;
	unsigned int mac_lo, mac_hi;
	unsigned int status;
	u8 mac[ETH_ALEN];
	int ret;

	/* Check device presence */
	status = t4net_readl(T4NET_REG_STATUS);
	printk("t4net: status=0x%x\n", status);

	dev = alloc_etherdev(sizeof(struct t4net_priv));
	if (!dev)
		return -ENOMEM;

	priv = netdev_priv(dev);
	priv->dev = dev;

	/* Read MAC from device */
	mac_lo = t4net_readl(T4NET_REG_MAC_LO);
	mac_hi = t4net_readl(T4NET_REG_MAC_HI);
	mac[0] = (mac_lo >>  0) & 0xFF;
	mac[1] = (mac_lo >>  8) & 0xFF;
	mac[2] = (mac_lo >> 16) & 0xFF;
	mac[3] = (mac_lo >> 24) & 0xFF;
	mac[4] = (mac_hi >>  0) & 0xFF;
	mac[5] = (mac_hi >>  8) & 0xFF;
	eth_hw_addr_set(dev, mac);

	dev->netdev_ops = &t4net_netdev_ops;
	dev->flags &= ~IFF_MULTICAST;

	ret = register_netdev(dev);
	if (ret) {
		printk("t4net: register_netdev failed: %d\n", ret);
		free_netdev(dev);
		return ret;
	}

	t4net_dev = dev;
	printk("t4net: %s MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
	       dev->name,
	       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
	       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	if (status & T4NET_STATUS_LINK_UP)
		printk("t4net: link up (TAP connected)\n");
	else
		printk("t4net: link down (no TAP)\n");

	return 0;
}

device_initcall(t4net_init);
