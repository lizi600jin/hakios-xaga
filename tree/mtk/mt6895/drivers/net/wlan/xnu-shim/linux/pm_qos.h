/* XNU-compat shim: linux/pm_qos.h
 *
 * XNU has no pm_qos subsystem. soc7_0 only uses it to pin VCORE OPP while
 * the iCAP clock is raised, so the request object is opaque here and the
 * three calls are no-ops; the real pinning belongs to the XNU power domain.
 */
#ifndef _XAGA_SHIM_PM_QOS_H
#define _XAGA_SHIM_PM_QOS_H

struct pm_qos_request {
	int _xaga_unused;
};

static inline int pm_qos_request_active(struct pm_qos_request *req)
{
	(void)req;
	return 0;   /* never "active": the add path is taken on first use */
}

static inline void pm_qos_add_request(struct pm_qos_request *req,
				      int qos_class, int value)
{
	(void)req; (void)qos_class; (void)value;
}

static inline void pm_qos_update_request(struct pm_qos_request *req, int value)
{
	(void)req; (void)value;
}

static inline void pm_qos_remove_request(struct pm_qos_request *req)
{
	(void)req;
}

#endif
