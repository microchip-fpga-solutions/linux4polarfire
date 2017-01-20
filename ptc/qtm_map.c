#include <asm/types.h>
#include <stddef.h>
#include <stdio.h>
#define FW_V6
#include "../include/uapi/linux/atmel_ptc.h"

#define member_size(type, member) (sizeof(((type *)0)->member))
#define ADDR(field) (BASE_ADDR + offsetof(struct atmel_qtm_mailbox, field))

#define print_field_addr(field) \
do { \
	printf("0x%08lx: "#field"\n", ADDR(field)); \
} while(0)

#define print_subfield_addr(field, subfield) \
do { \
	printf("0x%08lx:   "#subfield" (u%lu alignment: %s)\n", \
	       ADDR(field.subfield), \
	       member_size(struct atmel_qtm_##field, subfield) * 8, \
	       ((ADDR(field.subfield) % member_size(struct atmel_qtm_##field, subfield)) ? "nok" : "ok") \
	      ); \
} while(0)

#define print_subfield_tab_addr(field, subfield, index) \
do { \
	printf("0x%08lx:   [%d] "#subfield" (u%lu alignment: %s)\n", \
	       ADDR(field[index].subfield), \
	       index, member_size(struct atmel_qtm_##field, subfield) * 8, \
	       ((ADDR(field[index].subfield) % member_size(struct atmel_qtm_##field, subfield)) ? "nok" : "ok") \
	      ); \
} while(0)

#define BASE_ADDR 0x804000


int main(void)
{
	int i;

	printf("=== QTM Mailbox Map ===\n");
	printf("size: 0x%lx\n", sizeof(struct atmel_qtm_mailbox));

	print_field_addr(cmd);
	print_subfield_addr(cmd, id);
	print_subfield_addr(cmd, addr);
	print_subfield_addr(cmd, data);

	print_field_addr(node_group_config);
	print_subfield_addr(node_group_config, count);
	print_subfield_addr(node_group_config, ptc_type);
	print_subfield_addr(node_group_config, freq_option);
	print_subfield_addr(node_group_config, calib_option);

	print_field_addr(node_config);
	for (i = 0; i < 2; i++) {
		print_subfield_tab_addr(node_config, mask_x, i);
		print_subfield_tab_addr(node_config, mask_y, i);
		print_subfield_tab_addr(node_config, csd, i);
		print_subfield_tab_addr(node_config, rsel, i);
		print_subfield_tab_addr(node_config, prsc, i);
		print_subfield_tab_addr(node_config, gain_analog, i);
		print_subfield_tab_addr(node_config, gain_digital, i);
		print_subfield_tab_addr(node_config, oversampling, i);
	}

	print_field_addr(node_data);
	for (i = 0; i < 2; i++) {
		print_subfield_tab_addr(node_data, status, i);
		print_subfield_tab_addr(node_data, signals, i);
		print_subfield_tab_addr(node_data, comp_caps, i);
	}

	print_field_addr(key_group_config);
	print_subfield_addr(key_group_config, count);
	print_subfield_addr(key_group_config, touch_di);
	print_subfield_addr(key_group_config, max_on_time);
	print_subfield_addr(key_group_config, anti_touch_di);
	print_subfield_addr(key_group_config, anti_touch_recal_thr);
	print_subfield_addr(key_group_config, touch_drift_rate);
	print_subfield_addr(key_group_config, anti_touch_drift_rate);
	print_subfield_addr(key_group_config, drift_hold_time);
	print_subfield_addr(key_group_config, reburst_mode);

	print_field_addr(key_config);
	for (i = 0; i < 2; i++) {
		print_subfield_tab_addr(key_config, threshold, i);
		print_subfield_tab_addr(key_config, hysteresis, i);
		print_subfield_tab_addr(key_config, aks_group, i);
	}
	
	print_field_addr(key_data);
	for (i = 0; i < 2; i++) {
		print_subfield_tab_addr(key_data, status, i);
		print_subfield_tab_addr(key_data, status_counter, i);
		print_subfield_tab_addr(key_data, node_struct_ptr, i);
		print_subfield_tab_addr(key_data, reference, i);
	}
	
	print_field_addr(auto_scan_config);
	print_subfield_addr(auto_scan_config, node_number);
	print_subfield_addr(auto_scan_config, node_threshold);
	print_subfield_addr(auto_scan_config, trigger);
	
	print_field_addr(scroller_group_config);
	print_subfield_addr(scroller_group_config, key_data);
	print_subfield_addr(scroller_group_config, count);
	
	print_field_addr(scroller_config);
	for (i = 0; i < 2; i++) {
		print_subfield_tab_addr(scroller_config, type, i);
		print_subfield_tab_addr(scroller_config, key_start, i);
		print_subfield_tab_addr(scroller_config, key_count, i);
		print_subfield_tab_addr(scroller_config, resol_deadband, i);
		print_subfield_tab_addr(scroller_config, position_hysteresis, i);
		print_subfield_tab_addr(scroller_config, contact_min_threshold, i);
	}

	print_field_addr(scroller_data);
	for (i = 0; i < 2; i++) {
		print_subfield_tab_addr(scroller_data, status, i);
		print_subfield_tab_addr(scroller_data, right_hyst, i);
		print_subfield_tab_addr(scroller_data, left_hyst, i);
		print_subfield_tab_addr(scroller_data, raw_position, i);
		print_subfield_tab_addr(scroller_data, position, i);
		print_subfield_tab_addr(scroller_data, contact_size, i);
	}

	print_field_addr(fh_autotune_config);
	print_subfield_addr(fh_autotune_config, count);
	print_subfield_addr(fh_autotune_config, num_freqs);
	print_subfield_addr(fh_autotune_config, freq_option_select);
	print_subfield_addr(fh_autotune_config, median_filter_freq);
	print_subfield_addr(fh_autotune_config, enable_freq_autotune);
	print_subfield_addr(fh_autotune_config, max_variance_limit);
	print_subfield_addr(fh_autotune_config, autotune_count_in_limit);

	print_field_addr(fh_autotune_data);
	print_subfield_addr(fh_autotune_data, status);
	print_subfield_addr(fh_autotune_data, current_freq);
	print_subfield_addr(fh_autotune_data, filter_buffer);
	print_subfield_addr(fh_autotune_data, acq_node_data);
	print_subfield_addr(fh_autotune_data, freq_tune_count_ins);

	print_field_addr(fh_freq);

	print_field_addr(touch_events);
	print_subfield_addr(touch_events, key_event_id);
	print_subfield_addr(touch_events, key_enable_state);
	print_subfield_addr(touch_events, scroller_event_id);
	print_subfield_addr(touch_events, scroller_event_state);

	return 0;
}
