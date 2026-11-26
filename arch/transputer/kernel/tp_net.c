/*
 * TPNet — Simple MMIO Ethernet driver for Transputer software emulator
 *
 * Talks to the emulator's built-in NIC at 0x30000000.
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

#define TPNET_BASE       0x30000000UL
#define TPNET_REG_STATUS 0x00
#define TPNET_REG_TX_LEN 0x04
#define TPNET_REG_RX_LEN 0x08
#define TPNET_REG_MAC_LO 0x0C
#define TPNET_REG_MAC_HI 0x10
#define TPNET_REG_RX_ACK 0x14
#define TPNET_TX_BUF     0x1000
#define TPNET_RX_BUF     0x2000
#define TPNET_BUF_SIZE   1536

#define TPNET_STATUS_LINK_UP  0x01
#define TPNET_STATUS_RX_READY 0x02
#define TPNET_STATUS_TX_READY 0x04

/* MMIO access — use volatile pointers (no ioremap on NOMMU transputer) */
static inline unsigned int tpnet_readl(unsigned int offset)
{
	return *(volatile unsigned int *)(TPNET_BASE + offset);
}

static inline void tpnet_writel(unsigned int val, unsigned int offset)
{
	*(volatile unsigned int *)(TPNET_BASE + offset) = val;
}

static inline unsigned char tpnet_readb(unsigned int offset)
{
	return *(volatile unsigned char *)(TPNET_BASE + offset);
}

static inline void tpnet_writeb(unsigned char val, unsigned int offset)
{
	*(volatile unsigned char *)(TPNET_BASE + offset) = val;
}

struct tpnet_priv {
	struct net_device *dev;
	struct timer_list poll_timer;
	struct net_device_stats stats;
};

/* RX poll — called from timer to check for incoming packets */
static void tpnet_rx_poll(struct net_device *dev)
{
	struct tpnet_priv *priv = netdev_priv(dev);
	unsigned int rx_len;
	struct sk_buff *skb;
	unsigned int i;
	unsigned char *data;

	rx_len = tpnet_readl(TPNET_REG_RX_LEN);
	if (rx_len == 0 || rx_len > TPNET_BUF_SIZE)
		return;

	skb = dev_alloc_skb(rx_len + 2);
	if (!skb) {
		priv->stats.rx_dropped++;
		tpnet_writel(1, TPNET_REG_RX_ACK);
		return;
	}

	skb_reserve(skb, 2);  /* align IP header */
	data = skb_put(skb, rx_len);

	/* Read packet from RX buffer byte-by-byte */
	for (i = 0; i < rx_len; i++)
		data[i] = tpnet_readb(TPNET_RX_BUF + i);

	/* Acknowledge — tell emulator we consumed the packet */
	tpnet_writel(1, TPNET_REG_RX_ACK);

	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_NONE;

	netif_rx(skb);
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += rx_len;
}

static void tpnet_poll_timer_fn(struct timer_list *timer)
{
	struct tpnet_priv *priv = timer_container_of(priv, timer, poll_timer);
	struct net_device *dev = priv->dev;

	tpnet_rx_poll(dev);

	/* Re-arm timer — poll every 10ms (HZ/100) */
	mod_timer(&priv->poll_timer, jiffies + (HZ / 100));
}

static int tpnet_open(struct net_device *dev)
{
	struct tpnet_priv *priv = netdev_priv(dev);

	/* Start RX poll timer */
	timer_setup(&priv->poll_timer, tpnet_poll_timer_fn, 0);
	mod_timer(&priv->poll_timer, jiffies + HZ);

	netif_start_queue(dev);
	printk("tpnet: interface up\n");
	return 0;
}

static int tpnet_stop(struct net_device *dev)
{
	struct tpnet_priv *priv = netdev_priv(dev);

	timer_delete_sync(&priv->poll_timer);
	netif_stop_queue(dev);
	printk("tpnet: interface down\n");
	return 0;
}

static netdev_tx_t tpnet_start_xmit(struct sk_buff *skb,
				     struct net_device *dev)
{
	struct tpnet_priv *priv = netdev_priv(dev);
	unsigned int len = skb->len;
	unsigned char *data = skb->data;
	unsigned int i;

	if (len > TPNET_BUF_SIZE) {
		dev_kfree_skb(skb);
		priv->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	/* Copy packet to TX buffer byte-by-byte */
	for (i = 0; i < len; i++)
		tpnet_writeb(data[i], TPNET_TX_BUF + i);

	/* Write TX length — triggers the send in the emulator */
	tpnet_writel(len, TPNET_REG_TX_LEN);

	priv->stats.tx_packets++;
	priv->stats.tx_bytes += len;

	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static struct net_device_stats *tpnet_get_stats(struct net_device *dev)
{
	struct tpnet_priv *priv = netdev_priv(dev);
	return &priv->stats;
}

static const struct net_device_ops tpnet_netdev_ops = {
	.ndo_open	= tpnet_open,
	.ndo_stop	= tpnet_stop,
	.ndo_start_xmit	= tpnet_start_xmit,
	.ndo_get_stats	= tpnet_get_stats,
};

static struct net_device *tpnet_dev;

static int __init tpnet_init(void)
{
	struct net_device *dev;
	struct tpnet_priv *priv;
	unsigned int mac_lo, mac_hi;
	unsigned int status;
	u8 mac[ETH_ALEN];
	int ret;

	/* Check device presence */
	status = tpnet_readl(TPNET_REG_STATUS);
	printk("tpnet: status=0x%x\n", status);

	dev = alloc_etherdev(sizeof(struct tpnet_priv));
	if (!dev)
		return -ENOMEM;

	priv = netdev_priv(dev);
	priv->dev = dev;

	/* Read MAC from device */
	mac_lo = tpnet_readl(TPNET_REG_MAC_LO);
	mac_hi = tpnet_readl(TPNET_REG_MAC_HI);
	mac[0] = (mac_lo >>  0) & 0xFF;
	mac[1] = (mac_lo >>  8) & 0xFF;
	mac[2] = (mac_lo >> 16) & 0xFF;
	mac[3] = (mac_lo >> 24) & 0xFF;
	mac[4] = (mac_hi >>  0) & 0xFF;
	mac[5] = (mac_hi >>  8) & 0xFF;
	eth_hw_addr_set(dev, mac);

	dev->netdev_ops = &tpnet_netdev_ops;
	dev->flags &= ~IFF_MULTICAST;

	ret = register_netdev(dev);
	if (ret) {
		printk("tpnet: register_netdev failed: %d\n", ret);
		free_netdev(dev);
		return ret;
	}

	tpnet_dev = dev;
	printk("tpnet: %s MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
	       dev->name,
	       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
	       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	if (status & TPNET_STATUS_LINK_UP)
		printk("tpnet: link up (TAP connected)\n");
	else
		printk("tpnet: link down (no TAP)\n");

	return 0;
}

device_initcall(tpnet_init);
