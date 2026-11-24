// SPDX-License-Identifier: GPL-2.0
/*
 * tcpts-strip —— 把出网 TCP 建连包里的「时间戳选项」就地抹掉
 *
 * 为什么需要它：
 *   TCP 时间戳（kind=8, len=10）由客户端自己的协议栈生成，TSval 是它的
 *   单调时钟（Linux 约 1kHz、Windows 约 10ms 一跳），开机时长也能从初值
 *   反推。一个 NAT 后面挂着多台机器时，网关只要按「同一公网 IP 的 TSval
 *   速率/初值是否连续」就能把一台拆成多台 —— TTL 和 IPID 都伪装了也没用。
 *
 * 做法：
 *   在 POST_ROUTING（NF_IP_PRI_NAT_SRC 之后）解析 TCP 头选项。对
 *   「带 SYN 且不带 ACK」的包，把 kind=8 的 10 字节选项整段写成 10 个
 *   NOP(0x01)，长度不变、doff 不变，只需重算 TCP 校验和。
 *
 *   只动 SYN 的原因：SYN 里没有 ts，对端 SYN-ACK 也不会回 ts，这条连接
 *   两个方向从此都不带时间戳 —— 网关侧彻底失去这条线索。反过来如果在
 *   已协商 ts 的连接上单方向剥离，TCSecr 会对不上，打乱对端的 PAWS 与
 *   RTT 采样，可能触发重传。
 *
 * 代价：
 *   每个建连包多一次头部解析 + 一次校验和计算，SYN 占比极低。
 *   失去 PAWS（防止序号回绕）与时间戳 RTT 采样，对家用/宿舍场景无感。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <net/tcp.h>

#ifndef TCPOLEN_TIMESTAMP
#define TCPOLEN_TIMESTAMP 10
#endif
#ifndef TCPOPT_TIMESTAMP
#define TCPOPT_TIMESTAMP 8
#endif
#ifndef TCPOPT_NOP
#define TCPOPT_NOP 1
#endif
#ifndef TCPOPT_EOL
#define TCPOPT_EOL 0
#endif

static unsigned int strip_syn = 1;
module_param(strip_syn, uint, 0644);
MODULE_PARM_DESC(strip_syn, "1 = strip TCP timestamp option from SYNs (default: 1)");

static unsigned long rebuilt;	/* 改写过校验和的包数 */
static unsigned long noopt;	/* SYN 里本来就没时间戳 */
static unsigned long skipped;	/* skb 不可写，跳过 */
static unsigned long reported;

static unsigned int hook_funcion(void *priv, struct sk_buff *skb,
				 const struct nf_hook_state *state)
{
	struct iphdr *iph;
	struct tcphdr *th;
	unsigned char *opt;
	unsigned int ihl, thl, optlen, i;
	unsigned int saddr, daddr;
	u32 tcp_len;
	__wsum csum, partial;
	int found = 0;

	if (!skb)
		return NF_ACCEPT;

	iph = ip_hdr(skb);
	if (iph->version != 4 || iph->protocol != IPPROTO_TCP)
		return NF_ACCEPT;
	/* 非首片不管：拿不到完整 TCP 头 */
	if (iph->frag_off & htons(IP_OFFSET))
		return NF_ACCEPT;

	ihl = (unsigned int)iph->ihl * 4;
	if (ihl < 20 || ihl > 60)
		return NF_ACCEPT;

	/* 先保证能读到 TCP 固定头（20 字节，含 doff） */
	if (skb_ensure_writable(skb, ihl + sizeof(struct tcphdr))) {
		skipped++;
		return NF_ACCEPT;
	}

	iph = ip_hdr(skb);
	th = (struct tcphdr *)((unsigned char *)iph + ihl);

	if (!th->syn || th->ack) {
		/* 不是建连包，原样放行（普通转发的绝大多数包走这里） */
		return NF_ACCEPT;
	}
	if (!strip_syn)
		return NF_ACCEPT;

	thl = (unsigned int)th->doff * 4;
	if (thl <= sizeof(struct tcphdr) || thl > 60) {
		noopt++;
		return NF_ACCEPT;
	}

	/* 现在需要整个 TCP 头（含 options）可写 */
	if (skb_ensure_writable(skb, ihl + thl)) {
		skipped++;
		return NF_ACCEPT;
	}

	iph = ip_hdr(skb);
	th = (struct tcphdr *)((unsigned char *)iph + ihl);
	opt = (unsigned char *)th + sizeof(struct tcphdr);
	optlen = thl - sizeof(struct tcphdr);

	for (i = 0; i < optlen;) {
		unsigned char kind = opt[i];
		unsigned char olen;

		if (kind == TCPOPT_EOL)
			break;
		if (kind == TCPOPT_NOP) {
			i++;
			continue;
		}
		if (i + 1 >= optlen)
			break;
		olen = opt[i + 1];
		if (olen < 2 || i + olen > optlen)
			break;

		if (kind == TCPOPT_TIMESTAMP && olen == TCPOLEN_TIMESTAMP) {
			/* 整段换成等长 NOP：doff 不变，后面 payload 不用挪 */
			memset(&opt[i], TCPOPT_NOP, olen);
			found = 1;
			break;
		}
		i += olen;
	}

	if (!found) {
		noopt++;
		return NF_ACCEPT;
	}

	/* 重算 TCP 校验和（IP 头没动，IP 校验和不用管） */
	tcp_len = (u32)ntohs(iph->tot_len) - ihl;
	/* 注意：纯 SYN 没有 payload，这时 tcp_len == thl 是正常的，只挡小于 */
	if (tcp_len < thl)
		return NF_ACCEPT;	/* 长度异常，放弃 */

	saddr = iph->saddr;
	daddr = iph->daddr;

	th->check = 0;
	skb->ip_summed = CHECKSUM_NONE;
	partial = skb_checksum(skb, ihl, (int)tcp_len, 0);
	th->check = tcp_v4_check((int)tcp_len, saddr, daddr, partial);

	rebuilt++;
	if (rebuilt >= reported) {
		reported = rebuilt * 2;
		pr_info("tcpts-strip: stripped %lu SYNs (no-opt %lu, unwritable %lu)\n",
			rebuilt, noopt, skipped);
	}

	return NF_ACCEPT;
}

static struct nf_hook_ops nfho = {
	.hook = hook_funcion,
	.pf = NFPROTO_IPV4,
	.hooknum = NF_INET_POST_ROUTING,
	.priority = NF_IP_PRI_NAT_SRC,
};

static int __init tcpts_strip_init(void)
{
	int ret;

	ret = nf_register_net_hook(&init_net, &nfho);
	if (ret) {
		pr_err("tcpts-strip: nf_register_net_hook failed: %d\n", ret);
		return ret;
	}
	pr_info("tcpts-strip: Started, version=%d, strip_syn=%u.\n",
		VERSION, strip_syn);
	return 0;
}

static void __exit tcpts_strip_exit(void)
{
	nf_unregister_net_hook(&init_net, &nfho);
	pr_info("tcpts-strip: Stopped, rebuilt=%lu, no-opt=%lu, unwritable=%lu.\n",
		rebuilt, noopt, skipped);
}

module_init(tcpts_strip_init);
module_exit(tcpts_strip_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("for personal OpenWrt use");
MODULE_DESCRIPTION("Strip TCP timestamp option from outgoing SYNs");
