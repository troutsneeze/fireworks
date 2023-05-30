#include <allegro5/allegro.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_shader.h>
#include <allegro5/allegro_memfile.h>
#include <allegro5/allegro_image.h>

#include <list>
#include <math.h>
#include <stdio.h>

#include "icon.h"

const double MIN_TIME = 0.2;
const double MAX_TIME = 0.4;
double START_V;
double END_V;
double AVG_V;
const double SMALL_V = 75;
const int MAX_SPREAD = 35;
const int MIN_SPREAD = 6;
const double A_DEC = 0.5;
const int BS = 2;
const int SS = 2;

extern const char *hlsl_bloom;
extern const char *glsl_bloom;

struct Fragment
{
	double x, y;
	double vx, vy;
	ALLEGRO_COLOR color;
	double gravity;
	double size;
};

inline double myrand()
{
	return ((double)(rand() % RAND_MAX)/RAND_MAX);
}

inline ALLEGRO_COLOR randcolor()
{
	int red = 0;
	int green = 0;
	int blue = 0;

	int which = myrand() * 5;

	if (which == 0) {
		red = 150 + myrand() * 100;
	}
	else if (which == 1) {
		red = 150 + myrand() * 100;
		green = 150 + myrand() * 100;
	}
	else if (which == 2) {
		red = 150 + myrand() * 100;
		green = 150 + myrand() * 100;
		blue = 150 + myrand() * 100;
	}
	else if (which == 3) {
		green = 150 + myrand() * 100;
		blue = 150 + myrand() * 100;
	}
	else {
		blue = 150 + myrand() * 100;
	}

	return al_map_rgb(red, green, blue);
}

int main(int argc, char **argv)
{
	al_init();
	al_init_primitives_addon();
	al_init_image_addon();
	al_install_keyboard();

	al_set_new_display_flags(ALLEGRO_FULLSCREEN_WINDOW);
#ifndef ALLEGRO_NUTHIN
	al_set_new_display_flags(al_get_new_display_flags()|ALLEGRO_OPENGL);
#endif

	ALLEGRO_DISPLAY *display = al_create_display(0, 0);

	ALLEGRO_FILE *icon_f = al_open_memfile(___fireworks_png, ___fireworks_png_len, "rb");
	ALLEGRO_BITMAP *icon_bmp = al_load_bitmap_f(icon_f, ".png");
	al_fclose(icon_f);
	al_set_display_icon(display, icon_bmp);
	al_destroy_bitmap(icon_bmp);

	double W = al_get_display_width(display);
	double H = al_get_display_height(display);

	START_V = H * 0.375;
	END_V = START_V - 7;
	AVG_V = (START_V+END_V)/2.0;

	ALLEGRO_BITMAP *buffer = al_create_bitmap(W, H);
	al_set_target_bitmap(buffer);
	al_clear_to_color(al_map_rgb(0, 0, 0));
	
	al_install_audio();
	al_reserve_samples(100);

	// Generate some sound effects
	const int SAMPLE_RATE = 44100; 
	const int BOOM_SAMPLES = SAMPLE_RATE/2;
	const int BOOM_BYTES = BOOM_SAMPLES * 2;
	const int LAUNCH_SAMPLES = SAMPLE_RATE;
	const int LAUNCH_BYTES = LAUNCH_SAMPLES * 2;
	const int SIGNED_RANGE = pow(2, 15);
	const int UNSIGNED_RANGE = pow(2, 16);

	int16_t *boom_buf = (int16_t *)al_malloc(BOOM_BYTES);
	int16_t *launch_buf = (int16_t *)al_malloc(LAUNCH_BYTES);

	const int repeat = 20;
	for (int i = 0; i < BOOM_SAMPLES; i += repeat) {
		float mul;
		if (i >= BOOM_SAMPLES*3/4) {
			mul = 1.0-(float)(i-BOOM_SAMPLES*3/4)/(BOOM_SAMPLES/4);
		}
		else {
			mul = 1.0;
		}
		int16_t value = (myrand() * (UNSIGNED_RANGE*0.5) - (SIGNED_RANGE*0.5));
		for (int j = 0; j < repeat; j++) {
			boom_buf[i+j] = value * mul;
		}
	}

	float thing = 0.0;
	for (int i = 0; i < LAUNCH_SAMPLES; i++) {
		float mul;
		if (i >= LAUNCH_SAMPLES*3/4) {
			mul = 1.0-(float)(i-LAUNCH_SAMPLES*3/4)/(LAUNCH_SAMPLES/4);
		}
		else {
			mul = 1.0;
		}
		int16_t value = sin(thing) * (SIGNED_RANGE*0.5) * mul;
		launch_buf[i] = value;
		thing += 0.1 * (1.0 - ((float)i/LAUNCH_SAMPLES * 0.3));
	}
	
	ALLEGRO_SAMPLE *boom = al_create_sample(boom_buf, BOOM_SAMPLES, SAMPLE_RATE, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1, false);
	ALLEGRO_SAMPLE *launch = al_create_sample(launch_buf, LAUNCH_SAMPLES, SAMPLE_RATE, ALLEGRO_AUDIO_DEPTH_INT16, ALLEGRO_CHANNEL_CONF_1, false);

	std::list<Fragment> big;
	std::list<Fragment> sm;

	ALLEGRO_KEYBOARD_STATE kb_state;
	al_get_keyboard_state(&kb_state);

#ifdef ALLEGRO_NUTHIN
	ALLEGRO_SHADER *bloom = al_create_shader(ALLEGRO_SHADER_HLSL);
	al_attach_shader_source(
		bloom,
		ALLEGRO_PIXEL_SHADER,
		hlsl_bloom 
	);
#else
	ALLEGRO_SHADER *bloom = al_create_shader(ALLEGRO_SHADER_GLSL);
	al_attach_shader_source(
		bloom,
		ALLEGRO_PIXEL_SHADER,
		glsl_bloom 
	);
#endif
   	al_link_shader(bloom);

	const int peaks = 8;

	float mountains[(peaks*2+2)*2];
	mountains[0] = 0;
	mountains[1] = H-1;
	mountains[2] = W-1;
	mountains[3] = H-1;
	int vx = W-1;
	for (int i = 0; i < 32; i += 4) {
		int first = (W/peaks) * (myrand()/4 + 0.25);
		mountains[4+i+0] = vx;
		mountains[4+i+1] = H-100-myrand()*100;
		vx -= first;
		mountains[4+i+2] = vx;
		mountains[4+i+3] = H-100-myrand()*250;
		vx -= (W/peaks) - first;
	}

	const int num_stars = 1000;
	ALLEGRO_VERTEX stars[num_stars];
	for (int i = 0; i < num_stars; i++) {
		stars[i].x = W*myrand();
		stars[i].y = (H-200)*myrand();
		stars[i].z = 0;
		if (myrand() * num_stars < (num_stars/10)) {
			int v = myrand() * 200;
			stars[i].color = al_map_rgb(v, v, v);
		}
		else {
			int v[3];
			for (int j = 0; j < 3; j++) {
				v[j] = myrand() * 60;
			}
			if (myrand() * 2 == 0) {
				int j = myrand() * 3;
				v[j] += myrand() * 60;
			}
			stars[i].color = al_map_rgb(v[0], v[1], v[2]);
		}
	}

	srand(time(0));

	double loop_start = al_current_time();
	double next_start = loop_start;
	double next_draw = loop_start+(1.0/60.0);

	while (!al_key_down(&kb_state, ALLEGRO_KEY_ESCAPE)) {
		double now = al_current_time();
		if (now > next_draw) {
			// logic
			if (now > next_start) {
				Fragment f;
				f.x = myrand() * W;
				f.y = H;
				f.vx = (myrand()*30)-15;
				f.vy = END_V + ((START_V-END_V)/2*myrand()) + (START_V-END_V)/2;
				f.color = randcolor();
				f.size = BS + BS*myrand();
				al_play_sample(launch, 0.5+0.5*myrand(), 0.5, 1.0, ALLEGRO_PLAYMODE_ONCE, NULL);
				big.push_back(f);
				next_start = now + MIN_TIME + myrand()*(MAX_TIME-MIN_TIME);
			}
			double elapsed = now - loop_start;
			loop_start = now;
			std::list<Fragment>::iterator it;
			for (it = big.begin(); it != big.end();) {
				Fragment &f = *it;
				f.x += f.vx * elapsed;
				f.y -= f.vy * elapsed;
				f.vy -= (H/AVG_V) * elapsed;
				if (f.vy < END_V) {
					double a = 0;
					double r = myrand();
					int n = MIN_SPREAD + (MAX_SPREAD-MIN_SPREAD)*r;
					int i = 0;
					double v = (SMALL_V/2) + myrand()*(SMALL_V/2);
					double gravity = 40 * myrand();
					double size = SS + SS*myrand();
					do {
						Fragment s;
						s.x = f.x;
						s.y = f.y;
						s.vx = cos(a)*v;
						s.vy = sin(a)*v;
						s.color = f.color;
						s.gravity = gravity + 10 * myrand();
						s.size = size;
						al_play_sample(boom, 0.5+0.5*myrand(), 0.5, 1.0, ALLEGRO_PLAYMODE_ONCE, NULL);
						sm.push_back(s);
						a += (M_PI*2) / n;
						i++;
					} while (i < n);
					// Sometimes add a double ring
					if (rand() % 8 == 0) {
						a = 0;
						i = 0;
						v *= 0.75;
						n /= 2;
						if (n < MIN_SPREAD)
							n = MIN_SPREAD;
						ALLEGRO_COLOR c = randcolor();
						size = SS + SS*myrand();
						do {
							Fragment s;
							s.x = f.x;
							s.y = f.y;
							s.vx = cos(a)*v;
							s.vy = sin(a)*v;
							s.color = c;
							s.gravity = gravity + 10 * myrand();
							s.size = size;
							//al_play_sample(boom, 0.5+0.5*myrand(), 0.5, 1.0, ALLEGRO_PLAYMODE_ONCE, NULL);
							sm.push_back(s);
							a += (M_PI*2) / n;
							i++;
						} while (i < n);
					}
					else if (rand() % 8 < 4) {
						size = SS + SS*myrand();
						while (n >= MIN_SPREAD && size > 0) {
							a = 0;
							i = 0;
							v *= 0.8;
							n -= 4;
							do {
								Fragment s;
								s.x = f.x;
								s.y = f.y;
								s.vx = cos(a)*v;
								s.vy = sin(a)*v;
								s.color = f.color;
								s.gravity = gravity + 10 * myrand();
								s.size = size;
								//al_play_sample(boom, 0.5+0.5*myrand(), 0.5, 1.0, ALLEGRO_PLAYMODE_ONCE, NULL);
								sm.push_back(s);
								a += (M_PI*2) / n;
								i++;
							} while (i < n);
							size -= 2;
						}
					}
					it = big.erase(it);
					continue;
				}
				it++;
			}

			for (it = sm.begin(); it != sm.end();) {
				Fragment &f = *it;
				f.x += f.vx * elapsed;
				f.y += f.vy * elapsed;
				f.vy += f.gravity * elapsed; // hardcoded D:
				f.color.a -= A_DEC * elapsed;
				if (f.color.a < 0) {
					it = sm.erase(it);
					continue;
				}
				it++;
			}

			// draw
			float alpha = 100.0/255.0;
			al_set_target_bitmap(buffer);
			al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
			al_draw_prim(stars, 0, 0, 0, num_stars, ALLEGRO_PRIM_POINT_LIST);
			al_draw_filled_circle(150, 150, 60, al_map_rgb(100, 100, 50));
			al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);
			ALLEGRO_VERTEX sv[sm.size()*6];
			int v = 0;
			for (it = sm.begin(); it != sm.end(); it++) {
				Fragment& f = *it;
				ALLEGRO_COLOR color = al_map_rgba_f(f.color.r, f.color.g, f.color.b, alpha*f.color.a);
				sv[v].x = f.x - f.size/2;
				sv[v].y = f.y - f.size/2;
				sv[v].z = 0;
				sv[v].color = color;
				v++;
				sv[v].x = f.x + f.size/2;
				sv[v].y = f.y - f.size/2;
				sv[v].z = 0;
				sv[v].color = color;
				v++;
				sv[v].x = f.x - f.size/2;
				sv[v].y = f.y + f.size/2;
				sv[v].z = 0;
				sv[v].color = color;
				v++;
				sv[v].x = f.x + f.size/2;
				sv[v].y = f.y - f.size/2;
				sv[v].z = 0;
				sv[v].color = color;
				v++;
				sv[v].x = f.x + f.size/2;
				sv[v].y = f.y + f.size/2;
				sv[v].z = 0;
				sv[v].color = color;
				v++;
				sv[v].x = f.x - f.size/2;
				sv[v].y = f.y + f.size/2;
				sv[v].z = 0;
				sv[v].color = color;
				v++;
			}
			if (v > 0) {
				al_draw_prim(sv, 0, 0, 0, v, ALLEGRO_PRIM_TRIANGLE_LIST);
			}
			ALLEGRO_VERTEX bv[big.size()*6];
			v = 0;
			for (it = big.begin(); it != big.end(); it++) {
				Fragment& f = *it;
				ALLEGRO_COLOR color = al_map_rgba_f(f.color.r, f.color.g, f.color.b, alpha*f.color.a);
				bv[v].x = f.x - f.size/2;
				bv[v].y = f.y - f.size/2;
				bv[v].z = 0;
				bv[v].color = color;
				v++;
				bv[v].x = f.x + f.size/2;
				bv[v].y = f.y - f.size/2;
				bv[v].z = 0;
				bv[v].color = color;
				v++;
				bv[v].x = f.x - f.size/2;
				bv[v].y = f.y + f.size/2;
				bv[v].z = 0;
				bv[v].color = color;
				v++;
				bv[v].x = f.x + f.size/2;
				bv[v].y = f.y - f.size/2;
				bv[v].z = 0;
				bv[v].color = color;
				v++;
				bv[v].x = f.x + f.size/2;
				bv[v].y = f.y + f.size/2;
				bv[v].z = 0;
				bv[v].color = color;
				v++;
				bv[v].x = f.x - f.size/2;
				bv[v].y = f.y + f.size/2;
				bv[v].z = 0;
				bv[v].color = color;
				v++;
			}
			if (v > 0) {
				al_draw_prim(bv, 0, 0, 0, v, ALLEGRO_PRIM_TRIANGLE_LIST);
			}
			al_draw_filled_rectangle(0, 0, W, H, al_map_rgba_f(0, 0, 0, 0.075));
			
			al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ZERO);
			al_set_target_bitmap(al_get_backbuffer(display));
			
			al_set_shader_sampler(bloom, "buf", buffer, 0);
			al_use_shader(bloom, true);
			al_draw_bitmap(buffer, 0, 0, 0);
			al_use_shader(bloom, false);

			al_draw_filled_polygon(mountains, peaks*2+2, al_map_rgb(50, 30, 70));

			al_flip_display();
			next_draw = now + (1.0/60.0);
		}

		al_get_keyboard_state(&kb_state);

		al_rest(0.001);
	}

	al_stop_samples();

	al_destroy_shader(bloom);
	al_destroy_sample(boom);
	al_destroy_sample(launch);
}

const char *hlsl_bloom =
	"sampler2D buf;"
	""
	"float4 ps_main(float2 tex : TEXCOORD0) : COLOR0"
	"{"
	"   float4 colors[8];"
	"   float4 current;"
	"   float4 result;"
	""
	"   current = tex2D(buf, float2(tex.x, tex.y));"
	""
	"   if ((current.r + current.g + current.b) < 20.0f/255.0f) {"
	"      result = float4(0, 0, 0, 1);"
	"   }"
	"   else {"
	"      colors[0] = tex2D(buf, float2(tex.x-0.00125f, tex.y-0.001666f));"
	"      colors[1] = tex2D(buf, float2(tex.x, tex.y-0.00125f));"
	"      colors[2] = tex2D(buf, float2(tex.x+0.00125f, tex.y-0.001666f));"
	"      colors[3] = tex2D(buf, float2(tex.x-0.00125f, tex.y));"
	"      colors[4] = tex2D(buf, float2(tex.x+0.00125f, tex.y));"
	"      colors[5] = tex2D(buf, float2(tex.x-0.00125f, tex.y+0.001666f));"
	"      colors[6] = tex2D(buf, float2(tex.x, tex.y+0.00125f));"
	"      colors[7] = tex2D(buf, float2(tex.x+0.00125f, tex.y+0.001666f));"
	"   "
	"      result = float4("
	"         (colors[0].r + colors[1].r + colors[2].r + colors[3].r + colors[4].r + colors[5].r + colors[6].r + colors[7].r) / 10.0f + current.r,"
	"         (colors[0].g + colors[1].g + colors[2].g + colors[3].g + colors[4].g + colors[5].g + colors[6].g + colors[7].g) / 10.0f + current.g,"
	"         (colors[0].b + colors[1].b + colors[2].b + colors[3].b + colors[4].b + colors[5].b + colors[6].b + colors[7].b) / 10.0f + current.b,"
	"         (colors[0].a + colors[1].a + colors[2].a + colors[3].a + colors[4].a + colors[5].a + colors[6].a + colors[7].a) / 10.0f + current.a"
	"      );"
	"   }"
	""
	"   return result;"
	"}";

const char *glsl_bloom =
	"uniform sampler2D buf;"
	""
	"void main()"
	"{"
	"   vec4 colors[8];"
	"   vec4 current;"
	"   vec4 result;"
	""
	"   current = texture2D(buf, vec2(gl_TexCoord[0].x, gl_TexCoord[0].y));"
	""
	"   if ((current.r + current.g + current.b) < 20.0/255.0) {"
	"      result = vec4(0.0, 0.0, 0.0, 1.0);"
	"   }"
	"   else {"
	"      colors[0] = texture2D(buf, vec2(gl_TexCoord[0].x-0.00125, gl_TexCoord[0].y-0.001666));"
	"      colors[1] = texture2D(buf, vec2(gl_TexCoord[0].x, gl_TexCoord[0].y-0.00125));"
	"      colors[2] = texture2D(buf, vec2(gl_TexCoord[0].x+0.00125, gl_TexCoord[0].y-0.001666));"
	"      colors[3] = texture2D(buf, vec2(gl_TexCoord[0].x-0.00125, gl_TexCoord[0].y));"
	"      colors[4] = texture2D(buf, vec2(gl_TexCoord[0].x+0.00125, gl_TexCoord[0].y));"
	"      colors[5] = texture2D(buf, vec2(gl_TexCoord[0].x-0.00125, gl_TexCoord[0].y+0.001666));"
	"      colors[6] = texture2D(buf, vec2(gl_TexCoord[0].x, gl_TexCoord[0].y+0.00125));"
	"      colors[7] = texture2D(buf, vec2(gl_TexCoord[0].x+0.00125, gl_TexCoord[0].y+0.001666));"
	"   "
	"      result = vec4("
	"         (colors[0].r + colors[1].r + colors[2].r + colors[3].r + colors[4].r + colors[5].r + colors[6].r + colors[7].r) / 10.0 + current.r,"
	"         (colors[0].g + colors[1].g + colors[2].g + colors[3].g + colors[4].g + colors[5].g + colors[6].g + colors[7].g) / 10.0 + current.g,"
	"         (colors[0].b + colors[1].b + colors[2].b + colors[3].b + colors[4].b + colors[5].b + colors[6].b + colors[7].b) / 10.0 + current.b,"
	"         (colors[0].a + colors[1].a + colors[2].a + colors[3].a + colors[4].a + colors[5].a + colors[6].a + colors[7].a) / 10.0 + current.a"
	"      );"
	"   }"
	""
	"   gl_FragColor = result;"
	"}";
