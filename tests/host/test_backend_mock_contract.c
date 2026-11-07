/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_backend_mock_contract.c
 * Description: Host contracts for the KTM backend mocks. Each mock is linked
 *              instead of the production backend and observed only through
 *              the public facade in includes/ir0/.
 */

#include "test_harness_ir0.h"
#include <ir0/audio_backend.h>
#include <ir0/console_backend.h>
#include <ir0/input_backend.h>
#include <ir0/mm_port.h>
#include <ir0/video_backend.h>
#include <ktm_mock_audio.h>
#include <ktm_mock_console.h>
#include <ktm_mock_input.h>
#include <ktm_mock_mm_port.h>
#include <ktm_mock_platform.h>
#include <ktm_mock_video.h>

void test_console_backend_mock_contract(void)
{
	TEST_BEGIN("console_backend_mock_contract");

	ktm_console_mock_reset();
	ASSERT_EQ(console_backend_printk_to_screen(), 1);
	ASSERT_EQ(console_backend_uses_framebuffer(), 0);
	ASSERT_EQ(console_backend_fb_scale(), 1);
	ASSERT_EQ(console_backend_cursor_x(), 0);

	console_backend_init();
	console_backend_typewriter_init();
	ASSERT_EQ(ktm_console_mock_init_calls(), 1);
	ASSERT_EQ(ktm_console_mock_typewriter_init_calls(), 1);

	console_backend_write("hello", 5, 0x0A);
	ASSERT_EQ((int)ktm_console_mock_write_bytes(), 5);
	ASSERT_STR_EQ(ktm_console_mock_text(), "hello");
	ASSERT_EQ((int)ktm_console_mock_last_color(), 0x0A);

	/* NULL is a no-op, matching the production facade. */
	console_backend_write(NULL, 4, 0x0C);
	ASSERT_EQ((int)ktm_console_mock_write_bytes(), 5);

	console_backend_write_serial("ser", 3);
	ASSERT_EQ((int)ktm_console_mock_serial_bytes(), 3);
	ASSERT_STR_EQ(ktm_console_mock_serial_text(), "ser");

	/* Disk mirroring off drops serial bytes but not screen bytes. */
	console_backend_set_tty_serial_mirror(0);
	console_backend_write_serial("drop", 4);
	ASSERT_EQ((int)ktm_console_mock_serial_bytes(), 3);
	ASSERT_STR_EQ(ktm_console_mock_serial_text(), "ser");
	console_backend_set_tty_serial_mirror(1);
	console_backend_write_serial("!", 1);
	ASSERT_STR_EQ(ktm_console_mock_serial_text(), "ser!");

	console_backend_clear(0x1F);
	ASSERT_EQ(ktm_console_mock_clear_calls(), 1);
	ASSERT_EQ((int)ktm_console_mock_last_color(), 0x1F);

	console_backend_scroll(3);
	console_backend_scroll(2);
	ASSERT_EQ(ktm_console_mock_scroll_lines(), 5);

	console_backend_show_cursor(0x0B);
	ASSERT_EQ(ktm_console_mock_cursor_shows(), 1);
	ASSERT_EQ((int)ktm_console_mock_last_color(), 0x0B);

	ktm_console_mock_set_framebuffer(1);
	ktm_console_mock_set_fb_scale(2);
	ktm_console_mock_set_cursor_x(42);
	ASSERT_EQ(console_backend_uses_framebuffer(), 1);
	ASSERT_EQ(console_backend_fb_scale(), 2);
	ASSERT_EQ(console_backend_cursor_x(), 42);

	/* Handoff disables printk-to-screen; the panic path re-enables it. */
	console_backend_userspace_handoff();
	ASSERT_EQ(ktm_console_mock_handoff_calls(), 1);
	ASSERT_EQ(console_backend_printk_to_screen(), 0);
	console_backend_panic_screen_on();
	ASSERT_EQ(ktm_console_mock_panic_screen_calls(), 1);
	ASSERT_EQ(console_backend_printk_to_screen(), 1);

	TEST_END();
}

void test_input_backend_mock_contract(void)
{
	ir0_mouse_state_t state;
	ir0_mouse_state_t readback;
	uint8_t packet[3] = {0x09, 0xFE, 0x02};
	uint8_t read_packet[3] = {0, 0, 0};

	TEST_BEGIN("input_backend_mock_contract");

	ktm_input_mock_reset();
	ASSERT_EQ(input_kbd_has_data(), 0);
	ASSERT_EQ(input_kbd_get(), 0);
	ASSERT_EQ(input_kbd_get_layout(), IR0_KBD_LAYOUT_US);
	ASSERT_STR_EQ(input_kbd_get_layout_name(input_kbd_get_layout()), "us");
	ASSERT_EQ(input_kbd_get_console_mode(), IR0_INPUT_KBD_XLATE);

	ktm_input_mock_queue_key('a');
	ktm_input_mock_queue_key('b');
	ASSERT_EQ(input_kbd_has_data(), 1);
	ASSERT_EQ(input_kbd_get(), 'a');
	ASSERT_EQ(input_kbd_get(), 'b');
	ASSERT_EQ(input_kbd_get(), 0);

	ktm_input_mock_queue_key('x');
	input_kbd_clear();
	ASSERT_EQ(input_kbd_has_data(), 0);

	ASSERT_EQ(input_kbd_set_layout(IR0_KBD_LAYOUT_LATAM), 0);
	ASSERT_EQ(input_kbd_get_layout(), IR0_KBD_LAYOUT_LATAM);
	ASSERT_STR_EQ(input_kbd_get_layout_name(IR0_KBD_LAYOUT_LATAM), "latam");
	ASSERT_EQ(input_kbd_set_layout(7), -1);
	ASSERT_EQ(input_kbd_get_layout(), IR0_KBD_LAYOUT_LATAM);

	ASSERT_EQ(input_kbd_set_console_mode(IR0_INPUT_KBD_MEDIUMRAW), 0);
	ASSERT_EQ(input_kbd_get_console_mode(), IR0_INPUT_KBD_MEDIUMRAW);
	ASSERT_EQ(input_kbd_set_console_mode(1), -1);
	ASSERT_EQ(input_kbd_get_console_mode(), IR0_INPUT_KBD_MEDIUMRAW);

	input_kbd_resync_modifiers();
	input_kbd_poll_ps2();
	input_kbd_wake_readers();
	ASSERT_EQ(ktm_input_mock_resync_calls(), 1);
	ASSERT_EQ(ktm_input_mock_poll_calls(), 1);
	ASSERT_EQ(ktm_input_mock_wake_calls(), 1);

	/* Mouse unavailable: every success path reports failure honestly. */
	ASSERT(!input_mouse_is_available());
	ASSERT(!input_mouse_get_state(&readback));
	ASSERT(!input_mouse_set_sensitivity(80));
	ASSERT(!input_mouse_packet_available());
	ASSERT_EQ(input_mouse_read_ps2(read_packet), 0);
	input_mouse_feed_byte(0xAB);
	input_mouse_handle_interrupt();
	ASSERT_EQ(ktm_input_mock_mouse_feed_bytes(), 1);
	ASSERT_EQ((int)ktm_input_mock_last_aux_byte(), 0xAB);
	ASSERT_EQ(ktm_input_mock_mouse_interrupts(), 1);

	ktm_input_mock_set_mouse_available(1);
	ASSERT(input_mouse_is_available());

	state.x = 10;
	state.y = -5;
	state.wheel = 1;
	state.left_button = true;
	state.right_button = false;
	state.middle_button = true;
	state.button4 = false;
	state.button5 = true;
	state.has_wheel = true;
	state.has_5buttons = true;
	state.resolution = 4;
	state.sample_rate = 100;
	state.type = 3;
	state.initialized = true;
	ktm_input_mock_set_mouse_state(&state);
	ASSERT(input_mouse_get_state(&readback));
	ASSERT_EQ(readback.x, 10);
	ASSERT_EQ(readback.y, -5);
	ASSERT_EQ(readback.wheel, 1);
	ASSERT(readback.left_button);
	ASSERT(readback.middle_button);
	ASSERT(readback.button5);
	ASSERT(readback.initialized);

	ASSERT(input_mouse_set_sensitivity(80));
	ASSERT_EQ(ktm_input_mock_sensitivity(), 80);
	ktm_input_mock_set_mouse_sensitivity_result(0);
	ASSERT(!input_mouse_set_sensitivity(90));
	ASSERT_EQ(ktm_input_mock_sensitivity(), 90);

	ktm_input_mock_queue_mouse_packet(packet);
	ASSERT(input_mouse_packet_available());
	ASSERT_EQ(input_mouse_read_ps2(read_packet), 3);
	ASSERT_EQ(read_packet[0], 0x09);
	ASSERT_EQ(read_packet[1], 0xFE);
	ASSERT_EQ(read_packet[2], 0x02);
	ASSERT(!input_mouse_packet_available());
	ASSERT_EQ(input_mouse_read_ps2(read_packet), 0);

	TEST_END();
}

void test_video_backend_mock_contract(void)
{
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t bpp = 0;

	TEST_BEGIN("video_backend_mock_contract");

	ktm_video_mock_reset();
	ASSERT(!video_backend_is_available());
	ASSERT(!video_backend_get_info(&width, &height, &bpp));
	ASSERT(video_backend_get_fb() == NULL);

	ktm_video_mock_set_init_results(-1, 0);
	ASSERT_EQ(video_backend_init_from_boot(), -1);
	ASSERT(!video_backend_is_available());
	ASSERT_EQ(video_backend_init_fallback(), 0);
	ASSERT(video_backend_is_available());
	ASSERT_EQ(ktm_video_mock_boot_calls(), 1);
	ASSERT_EQ(ktm_video_mock_fallback_calls(), 1);

	ktm_video_mock_set_info(800, 600, 24);
	ktm_video_mock_set_pitch(2400);
	ktm_video_mock_set_fb_phys(0xE0000000u);
	ktm_video_mock_set_fb_size(8192);
	ktm_video_mock_set_fail_reason(7);

	ASSERT(video_backend_get_info(&width, &height, &bpp));
	ASSERT_EQ((int)width, 800);
	ASSERT_EQ((int)height, 600);
	ASSERT_EQ((int)bpp, 24);
	ASSERT_EQ(video_backend_get_pitch(), 2400u);
	ASSERT_EQ(video_backend_get_fb_phys(), 0xE0000000u);
	ASSERT_EQ(video_backend_get_fb_size(), 8192u);
	ASSERT_EQ(video_backend_fail_reason(), 7);
	ASSERT(video_backend_get_fb() == ktm_video_mock_framebuffer());

	ASSERT(!video_backend_get_info(NULL, &height, &bpp));
	ASSERT(!video_backend_get_info(&width, NULL, &bpp));
	ASSERT(!video_backend_get_info(&width, &height, NULL));

	/* A failed re-init does not silently drop a working mode. */
	ktm_video_mock_set_init_results(-1, -1);
	ASSERT_EQ(video_backend_init_from_boot(), -1);
	ASSERT(video_backend_is_available());

	ktm_video_mock_set_available(0);
	ASSERT(!video_backend_is_available());
	ASSERT(video_backend_get_fb() == NULL);
	ASSERT(!video_backend_get_info(&width, &height, &bpp));

	TEST_END();
}

void test_audio_backend_mock_contract(void)
{
	const uint8_t pcm[4] = {1, 2, 3, 4};

	TEST_BEGIN("audio_backend_mock_contract");

	ktm_audio_mock_reset();
	ASSERT(!audio_backend_is_available());
	ASSERT_EQ(audio_backend_play_pcm(NULL, 4, 48000, 2, 16), 0);
	ASSERT_EQ(audio_backend_play_pcm(pcm, 0, 48000, 2, 16), 0);
	ASSERT_EQ(ktm_audio_mock_play_calls(), 0);

	/* Without a codec the write is accepted but not programmed. */
	ASSERT_EQ(audio_backend_play_pcm(pcm, 4, 48000, 2, 16), 4);
	ASSERT_EQ(ktm_audio_mock_play_calls(), 1);
	ASSERT_EQ((int)ktm_audio_mock_last_count(), 4);
	ASSERT_EQ(ktm_audio_mock_last_sample_rate(), 48000u);
	ASSERT_EQ((int)ktm_audio_mock_last_channels(), 2);
	ASSERT_EQ((int)ktm_audio_mock_last_bits(), 16);

	ktm_audio_mock_set_available(1);
	ASSERT(audio_backend_is_available());
	ASSERT_EQ(audio_backend_play_pcm(pcm, 4, 44100, 1, 8), 4);
	ASSERT_EQ(ktm_audio_mock_play_calls(), 2);
	ASSERT_EQ(ktm_audio_mock_last_sample_rate(), 44100u);
	ASSERT_EQ((int)ktm_audio_mock_last_channels(), 1);
	ASSERT_EQ((int)ktm_audio_mock_last_bits(), 8);

	/* A codec failure is reported as -1, not as a short write. */
	ktm_audio_mock_set_play_result(-1);
	ASSERT_EQ(audio_backend_play_pcm(pcm, 4, 44100, 1, 8), -1);
	ASSERT_EQ(ktm_audio_mock_play_calls(), 3);

	audio_backend_set_master_volume(77);
	ASSERT_EQ((int)audio_backend_get_master_volume(), 77);
	audio_backend_speaker_on();
	ASSERT_EQ(ktm_audio_mock_speaker_on_calls(), 1);
	ASSERT_EQ(ktm_audio_mock_speaker_state(), 1);
	audio_backend_speaker_off();
	ASSERT_EQ(ktm_audio_mock_speaker_off_calls(), 1);
	ASSERT_EQ(ktm_audio_mock_speaker_state(), 0);

	TEST_END();
}

void test_mm_port_mock_contract(void)
{
	size_t total = 0;
	size_t used = 0;
	size_t free_frames = 0;
	size_t allocs = 0;

	TEST_BEGIN("mm_port_mock_contract");

	ktm_mm_port_mock_reset();
	ir0_mm_pmm_stats(&total, &used, &free_frames);
	ASSERT_EQ((int)total, 1024);
	ASSERT_EQ((int)used, 256);
	ASSERT_EQ((int)free_frames, 768);
	ASSERT_EQ(ktm_mm_port_mock_pmm_stats_calls(), 1);

	ktm_mm_port_mock_set_pmm_stats(10, 3, 7);
	ir0_mm_pmm_stats(&total, &used, &free_frames);
	ASSERT_EQ((int)total, 10);
	ASSERT_EQ((int)used, 3);
	ASSERT_EQ((int)free_frames, 7);

	/* NULL outputs are legal, matching the production facade. */
	ir0_mm_pmm_stats(NULL, NULL, NULL);
	ASSERT_EQ(ktm_mm_port_mock_pmm_stats_calls(), 3);

	ir0_mm_alloc_stats(&total, &used, &allocs);
	ASSERT_EQ((int)total, 4096);
	ASSERT_EQ((int)used, 1024);
	ASSERT_EQ((int)allocs, 7);
	ASSERT_EQ(ktm_mm_port_mock_alloc_stats_calls(), 1);

	ktm_mm_port_mock_set_alloc_stats(20, 5, 2);
	ir0_mm_alloc_stats(&total, &used, &allocs);
	ASSERT_EQ((int)total, 20);
	ASSERT_EQ((int)used, 5);
	ASSERT_EQ((int)allocs, 2);

	ktm_mm_port_mock_set_range(0x200000u, 0x5000000u);
	ASSERT_EQ((uintptr_t)ir0_mm_pmm_start(), (uintptr_t)0x200000u);
	ASSERT_EQ((uintptr_t)ir0_mm_pmm_end(), (uintptr_t)0x5000000u);

	TEST_END();
}

void test_platform_mock_contract(void)
{
	TEST_BEGIN("platform_mock_contract");

	ktm_platform_mock_reset();
	ASSERT(ktm_platform_mock_ops.halt != NULL);
	ASSERT(ktm_platform_mock_ops.reboot != NULL);
	ASSERT(ktm_platform_mock_ops.poweroff != NULL);

	ktm_platform_mock_ops.halt();
	ktm_platform_mock_ops.halt();
	ktm_platform_mock_ops.reboot();
	ktm_platform_mock_ops.poweroff();
	ASSERT_EQ(ktm_platform_mock_halt_count(), 2u);
	ASSERT_EQ(ktm_platform_mock_reboot_count(), 1u);
	ASSERT_EQ(ktm_platform_mock_poweroff_count(), 1u);

	ktm_platform_mock_reset();
	ASSERT_EQ(ktm_platform_mock_halt_count(), 0u);
	ASSERT_EQ(ktm_platform_mock_reboot_count(), 0u);
	ASSERT_EQ(ktm_platform_mock_poweroff_count(), 0u);

	TEST_END();
}
