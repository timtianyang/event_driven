/****************************************************************************
 *
 *        File: xc_rt.c
 *      Author: Sisu Xi
 *              Meng Xu
 *
 * Description: XC Interface to the rtds scheduler
 * Note: VCPU's parameter (period, budget) is in microsecond (us).
 *       All VCPUs of the same domain have same period and budget.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 */

#include "xc_private.h"

int xc_sched_rtds_domain_set(xc_interface *xch,
                           uint32_t domid,
                           struct xen_domctl_sched_rtds *sdom)
{
    int rc;
    DECLARE_DOMCTL;

    domctl.cmd = XEN_DOMCTL_scheduler_op;
    domctl.domain = (domid_t) domid;
    domctl.u.scheduler_op.sched_id = XEN_SCHEDULER_RTDS;
    domctl.u.scheduler_op.cmd = XEN_DOMCTL_SCHEDOP_putinfo;
    domctl.u.scheduler_op.u.rtds.period = sdom->period;
    domctl.u.scheduler_op.u.rtds.budget = sdom->budget;

    rc = do_domctl(xch, &domctl);

    return rc;
}

int xc_sched_rtds_domain_get(xc_interface *xch,
                           uint32_t domid,
                           struct xen_domctl_sched_rtds *sdom)
{
    int rc;
    DECLARE_DOMCTL;

    domctl.cmd = XEN_DOMCTL_scheduler_op;
    domctl.domain = (domid_t) domid;
    domctl.u.scheduler_op.sched_id = XEN_SCHEDULER_RTDS;
    domctl.u.scheduler_op.cmd = XEN_DOMCTL_SCHEDOP_getinfo;

    rc = do_domctl(xch, &domctl);

    if ( rc == 0 )
        *sdom = domctl.u.scheduler_op.u.rtds;

    return rc;
}

int
xc_sched_rtds_mc_set(
    xc_interface *xch,
    uint32_t domid,
    xen_domctl_mc_proto_t *mc_proto)
{
    int rc;
    DECLARE_DOMCTL;
    DECLARE_HYPERCALL_BOUNCE(
        mc_proto,
        sizeof(*mc_proto),
        XC_HYPERCALL_BUFFER_BOUNCE_IN);

    if ( xc_hypercall_bounce_pre(xch, mc_proto) )
        return -1;

    domctl.cmd = XEN_DOMCTL_scheduler_op;
    domctl.domain = (domid_t) domid;
    domctl.u.scheduler_op.sched_id = XEN_SCHEDULER_RTDS;
    domctl.u.scheduler_op.cmd = XEN_DOMCTL_SCHEDOP_putMC;
    
   
    set_xen_guest_handle(domctl.u.scheduler_op.u.v.proto, mc_proto);
    printf("in xc_mc_seti before do_domctl\n");
    rc = do_domctl(xch, &domctl);
    printf("after do_domctl");
    xc_hypercall_bounce_post(xch, mc_proto);
    printf("after hypercall_bounce\n");
    return rc;
}

