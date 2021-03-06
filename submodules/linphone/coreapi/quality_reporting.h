/*
linphone
Copyright (C) 2014 - Belledonne Communications, Grenoble, France

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef quality_reporting_h
#define quality_reporting_h

#include "linphonecore.h"

#ifdef __cplusplus
extern "C"{
#endif


/**
 * Linphone quality report sub object storing address related information (IP/port/MAC).
 */
typedef struct reporting_addr {
	char * ip;
	int port;
	uint32_t ssrc;
} reporting_addr_t;

/**
 * Linphone quality report sub object storing media metrics information as required by RFC035.
 */

typedef struct reporting_content_metrics {
	// timestamps - mandatory
	struct {
		time_t start;
		time_t stop;
	} timestamps;

	// session description - optional
	struct {
		int payload_type;
		char * payload_desc; // mime type
		int sample_rate; // clock rate
		int frame_duration; // to check (ptime?) - audio only
		char * fmtp;
		int packet_loss_concealment; // in voip metrics - audio only
	} session_description;

	// jitter buffet - optional
	struct {
		int adaptive; // constant
		int nominal; // average
		int max; // average
		int abs_max; // constant
	} jitter_buffer;

	// packet loss - optional
	struct {
		float network_packet_loss_rate; // average
		float jitter_buffer_discard_rate; // average
	} packet_loss;

	// delay - optional
	struct {
		int round_trip_delay; // no - vary
		int end_system_delay; // no - not implemented yet
		int symm_one_way_delay; // no - not implemented (depends on end_system_delay)
		int interarrival_jitter; // no - not implemented yet
		int mean_abs_jitter; // to check
	} delay;

	// signal - optional
	struct {
		int level; // no - vary
		int noise_level; // no - vary
	} signal;

	// quality estimates - optional
	struct {
		float moslq; // no - vary or avg - voip metrics - in [0..4.9]
		float moscq; // no - vary or avg - voip metrics - in [0..4.9]
	} quality_estimates;

	// for internal processing
	uint8_t rtcp_xr_count; // number of RTCP XR packets received since last report, used to compute average of instantaneous parameters as stated in the RFC 6035 (4.5)
	uint8_t rtcp_sr_count; // number of RTCP SR packets received since last report, used to compute RTT average values in case RTCP XR voip metrics is not enabled

} reporting_content_metrics_t;


/**
 * Linphone quality report main object created by function linphone_reporting_new().
 * It contains all fields required by RFC6035
 */
typedef struct reporting_session_report {
	struct {
		char * call_id;
		char * local_id;
		char * remote_id;
		char * orig_id;
		reporting_addr_t local_addr;
		reporting_addr_t remote_addr;
		char * local_group;
		char * remote_group;

		char * local_mac_addr; // optional
		char * remote_mac_addr; // optional
	} info;

	reporting_content_metrics_t local_metrics;
	reporting_content_metrics_t remote_metrics; // optional

	char * dialog_id; // optional

	// Quality of Service analyzer - custom extension
	/* This should allow us to analysis bad network conditions and quality adaptation
	on server side*/
	struct {
		char * name; /*type of the QoS analyzer used*/
		char* timestamp; /*time of each decision in seconds*/
		char* input_leg; /*input parameters' name*/
		char* input; /*set of inputs for each decision, semicolon separated*/
		char* output_leg; /*output parameters' name*/
		char* output; /*set of outputs for each decision, semicolon separated*/
	} qos_analyzer;

	// for internal processing
	time_t last_report_date;
} reporting_session_report_t;


typedef void (*LinphoneQualityReportingReportSendCb)(const LinphoneCall *call, int stream_type, const LinphoneContent *content);

reporting_session_report_t * linphone_reporting_new();
void linphone_reporting_destroy(reporting_session_report_t * report);

/**
 * Fill media information about a given call. This function must be called before
 * stopping the media stream.
 * @param call #LinphoneCall object to consider
 * @param stats_type the media type (LINPHONE_CALL_STATS_AUDIO or LINPHONE_CALL_STATS_VIDEO)
 *
 */
void linphone_reporting_update_media_info(LinphoneCall * call, int stats_type);

/**
 * Fill IP information about a given call. This function must be called each
 * time call state is 'LinphoneCallStreamsRunning' since IP might be updated (if we
 * found a direct route between caller and callee for example).
 * When call is starting, remote IP/port might be the proxy ones to which callee is registered
 * @param call #LinphoneCall object to consider
 *
 */
void linphone_reporting_update_ip(LinphoneCall * call);

/**
 * Publish a session report. This function should be called when session terminates,
 * media change (codec change or session fork), session terminates due to no media packets being received.
 * @param call #LinphoneCall object to consider
 * @param call_term whether the call has ended or is continuing
 *
 * @return error code. 0 for success, positive value otherwise.
 */
int linphone_reporting_publish_session_report(LinphoneCall* call, bool_t call_term);

/**
 * Publish an interval report. This function should be used for periodic interval
 * @param call #LinphoneCall object to consider
 * @return error code. 0 for success, positive value otherwise.
 *
 */
int linphone_reporting_publish_interval_report(LinphoneCall* call);

/**
 * Update publish reports with newly sent/received RTCP-XR packets (if available).
 * @param call #LinphoneCall object to consider
 * @param stats_type the media type (LINPHONE_CALL_STATS_AUDIO or LINPHONE_CALL_STATS_VIDEO)
 *
 */
void linphone_reporting_on_rtcp_update(LinphoneCall *call, int stats_type);

/**
 * Update publish reports on call state change.
 * @param call #LinphoneCall object to consider
 *
 */
void linphone_reporting_call_state_updated(LinphoneCall *call);

/**
 * Setter of the #LinphoneQualityReportingReportSendCb callback method which is
 * notified each time a report will be submitted to the collector, if quality
 * reporting is enabled
 * @param call #LinphoneCall object to consider
 * @param cb #LinphoneQualityReportingReportSendCb callback function to notify
 *
 */
void linphone_reporting_set_on_report_send(LinphoneCall *call, LinphoneQualityReportingReportSendCb cb);

#ifdef __cplusplus
}
#endif

#endif
