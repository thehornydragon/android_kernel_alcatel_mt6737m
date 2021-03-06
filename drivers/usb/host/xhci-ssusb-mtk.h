
#ifndef _XHCI_MTK_H_
#define _XHCI_MTK_H_

#include "xhci.h"

#define MAX_EP_NUM	32

struct sch_ep {
	int rh_port; /* root hub port number */
	/* device info */
	int speed;
	int is_tt;
	/* ep info */
	int is_in;
	int ep_type;
	int maxp;
	int interval;
	int burst;
	int mult;
	/* scheduling info */
	int offset;
	int repeat;
	int pkts;
	int cs_count;
	int burst_mode;
	/* other */
	int bw_cost; /* bandwidth cost in each repeat; including overhead */
	struct usb_host_endpoint *ep;
	struct xhci_ep_ctx *ep_ctx;
};

struct sch_port {
	struct sch_ep *ss_out_eps[MAX_EP_NUM];
	struct sch_ep *ss_in_eps[MAX_EP_NUM];
	struct sch_ep *hs_eps[MAX_EP_NUM];	/* including tt isoc */
	struct sch_ep *tt_intr_eps[MAX_EP_NUM];

	/* mtk xhci scheduling info */
};


#if IS_ENABLED(CONFIG_SSUSB_MTK_XHCI)

int xhci_mtk_init_quirk(struct xhci_hcd *xhci);
void xhci_mtk_exit_quirk(struct xhci_hcd *xhci);
int xhci_mtk_add_ep_quirk(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint *ep);
int xhci_mtk_drop_ep_quirk(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint *ep);
u32 xhci_mtk_td_remainder_quirk(unsigned int td_running_total,
	unsigned trb_buffer_length, struct urb *urb);

#else
static inline int xhci_mtk_init_quirk(struct xhci_hcd *xhci)
{
	return 0;
}

static inline void xhci_mtk_exit_quirk(struct xhci_hcd *xhci)
{
}

static inline int xhci_mtk_add_ep_quirk(struct usb_hcd *hcd,
	struct usb_device *udev, struct usb_host_endpoint *ep)
{
	return 0;
}

static inline int xhci_mtk_drop_ep_quirk(struct usb_hcd *hcd,
	struct usb_device *udev, struct usb_host_endpoint *ep)
{
	return 0;
}

u32 xhci_mtk_td_remainder_quirk(unsigned int td_running_total,
	unsigned trb_buffer_length, struct urb *urb)
{
	return 0;
}

#endif

#endif		/* _XHCI_MTK_H_ */
