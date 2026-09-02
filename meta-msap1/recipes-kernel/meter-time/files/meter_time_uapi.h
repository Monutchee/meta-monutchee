/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
#ifndef METER_TIME_UAPI_H
#define METER_TIME_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct meter_time_correlation {
	__u64 tai_before_nanoseconds;
	__u64 tai_after_nanoseconds;
	__u64 realtime_before_nanoseconds;
	__u64 realtime_after_nanoseconds;
	__u64 pl_tick;
	__u64 sample_index;
};

struct meter_time_ten_minute_boundary {
	__u64 target_sample_index;
	__u32 valid;
	__u32 reserved;
};

struct meter_time_frequency_10s_boundary {
	__u64 start_sample_index;
	__u64 end_sample_index;
	__u64 utc_start_nanoseconds;
	__u64 utc_end_nanoseconds;
	__u64 utc_uncertainty_nanoseconds;
	__u32 measured_sample_rate_millihz;
	__u32 boundary_generation;
	__u32 profile;
	__u32 flags;
	__u32 observer_status;
	__u32 completed_count;
	__u32 dropped_count;
	__u32 overflow_count;
	__u32 discontinuity_count;
	__u32 reserved;
};

#define METER_TIME_FREQUENCY_10S_BOUNDARY_VALID (1U << 0)
#define METER_TIME_FREQUENCY_10S_SYNCHRONIZED (1U << 1)

#define METER_TIME_IOC_MAGIC 'T'
#define METER_TIME_IOC_CORRELATE \
	_IOR(METER_TIME_IOC_MAGIC, 0x01, struct meter_time_correlation)
#define METER_TIME_IOC_SET_TEN_MINUTE_BOUNDARY \
	_IOW(METER_TIME_IOC_MAGIC, 0x02, struct meter_time_ten_minute_boundary)
#define METER_TIME_IOC_SET_FREQUENCY_10S_BOUNDARY \
	_IOWR(METER_TIME_IOC_MAGIC, 0x03, \
	      struct meter_time_frequency_10s_boundary)
#define METER_TIME_IOC_CANCEL_FREQUENCY_10S \
	_IO(METER_TIME_IOC_MAGIC, 0x04)

#endif /* METER_TIME_UAPI_H */
