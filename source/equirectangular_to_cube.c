#include <Windows.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define global        static
#define internal      static
#define local_persist static
#define zero_init          {0}
#define for_range(ident, lo, hi) for (int ident = (lo); ident <= (hi); ident += 1)
#define array_count(array) (sizeof ((array)) / sizeof ((array)[0]))
#define println(fmt, ...) { printf (fmt, __VA_ARGS__); printf ("\n"); }
#define error_out(fmt, ...) { println (fmt, __VA_ARGS__); return false; }
#define clamp(x, lo, hi) ((x) > (hi) ? (hi) : (x) <  (lo) ? (lo) : (x))
#define alignof(type) _Alignof (type)

#define TEMPORARY_STORAGE_SIZE 4 * 1024 * 1024

typedef          char       s8;
typedef unsigned char       u8;
typedef          short      s16;
typedef unsigned short      u16;
typedef          int        s32;
typedef unsigned int        u32;
typedef          long long  s64;
typedef unsigned long long  u64;

typedef enum u8 { false, true } bool;

typedef float  f32;
typedef double f64;

#define PI 3.1415926535897f

internal
f32 to_degs (f32 rads)
{
	return rads * 180 / PI;
}

internal
f32 to_rads (f32 degs)
{
	return degs * PI / 180;
}

// Modified version of: https://stackoverflow.com/a/29681646

#define MAX_PARALLEL_CONVERSIONS 6

typedef enum Face
{
	FACE_LEFT, FACE_FRONT, FACE_RIGHT, FACE_BACK, FACE_TOP, FACE_BOTTOM
} Face;

typedef union Color
{
	u8 n[4];
	u32 val;

	struct
	{
		u8 r, g, b, a;
	};
} Color;

typedef struct Image
{
	Color *data;
	u32    width;
	u32    height;
} Image;

typedef union Vec2i
{
	s32 n[2];

	struct
	{
		s32 x, y;
	};
} Vec2i;

typedef union Vec3
{
	f32 n[3];

	struct
	{
		f32 x, y, z;
	};
} Vec3;

internal
Vec3 vec3 (f32 x, f32 y, f32 z)
{
	Vec3 result;
	result.x = x;
	result.y = y;
	result.z = z;

	return result;
}

typedef union Vec4
{
	f32 n[4];

	struct
	{
		f32 x, y, z, w;
	};

	struct
	{
		f32 r, g, b, a;
	};
} Vec4;

internal
Vec4 vec4 (f32 x, f32 y, f32 z, f32 w)
{
	Vec4 result;
	result.x = x;
	result.y = y;
	result.z = z;
	result.w = w;

	return result;
}

// Maps a 2d coordinate (i, j), a face and an edge length to a point on a 3d cube
internal
Vec3 image_to_cube (int relative_i, int relative_j, Face face, int edge)
{
	f32 x = 2 * relative_i / (f32)edge - 1;
	f32 y = 2 * relative_j / (f32)edge - 1;

	switch (face)
	{
	case FACE_LEFT:   return vec3 ( x, -1, -y);
	case FACE_FRONT:  return vec3 ( 1,  x, -y);
	case FACE_RIGHT:  return vec3 (-x,  1, -y);
	case FACE_BACK:   return vec3 (-1, -x, -y);
	case FACE_TOP:    return vec3 ( y,  x,  1);
	case FACE_BOTTOM: return vec3 (-y,  x, -1); 
	}

	assert (false);
}

internal
Vec4 color_to_vec4_255 (Color col)
{
	return vec4 ((f32)col.r, (f32)col.g, (f32)col.b, (f32)col.a);
}

internal
Color vec4_255_to_color (Vec4 vec)
{
	Color result;
	result.r = (u8)vec.r;
	result.g = (u8)vec.g;
	result.b = (u8)vec.b;
	result.a = (u8)vec.a;

	return result;
}

internal
Color interpolate_colors (Color a, Color b, Color c, Color d, f32 ut, f32 vt)
{
	Vec4 av = color_to_vec4_255 (a);
	Vec4 bv = color_to_vec4_255 (b);
	Vec4 cv = color_to_vec4_255 (c);
	Vec4 dv = color_to_vec4_255 (d);

	Vec4 outv;
	outv.r = av.r * (1 - ut) * (1 - vt) + bv.r * ut * (1 - vt) + cv.r * (1 - ut) * vt + dv.r * ut * vt;
	outv.g = av.g * (1 - ut) * (1 - vt) + bv.g * ut * (1 - vt) + cv.g * (1 - ut) * vt + dv.g * ut * vt;
	outv.b = av.b * (1 - ut) * (1 - vt) + bv.b * ut * (1 - vt) + cv.b * (1 - ut) * vt + dv.b * ut * vt;
	outv.a = av.a * (1 - ut) * (1 - vt) + bv.a * ut * (1 - vt) + cv.a * (1 - ut) * vt + dv.a * ut * vt;

	return vec4_255_to_color (outv);
}

internal
void convert_face (Image in, Image out, Face face)
{
	int edge = in.width / 4;
	int i_lo, j_lo;
	int offset_from_top = out.height - in.width * 3 / 4;
	switch (face)
	{
	case FACE_LEFT:   i_lo = 0 * edge; j_lo = 1 * edge; break;
	case FACE_FRONT:  i_lo = 1 * edge; j_lo = 1 * edge; break;
	case FACE_RIGHT:  i_lo = 2 * edge; j_lo = 1 * edge; break;
	case FACE_BACK:   i_lo = 3 * edge; j_lo = 1 * edge; break;
	case FACE_TOP:    i_lo = 1 * edge; j_lo = 0 * edge; break;
	case FACE_BOTTOM: i_lo = 1 * edge; j_lo = 2 * edge; break;
	}

	for_range (i, i_lo, i_lo + edge - 1)
	{
		for_range (j, j_lo, j_lo + edge - 1)
		{
			// Transform (i, j) coordinates in ([0, out.width-1], [0, out.height - 1]) to ([0, 1], [0, 1])
			int relative_i = i;
			int relative_j = j;
			switch (face)
			{
			case FACE_LEFT:   relative_j -= edge; break;
			case FACE_FRONT:  relative_i -= edge; relative_j -= edge; break;
			case FACE_RIGHT:  relative_i -= 2 * edge; relative_j -= edge; break;
			case FACE_BACK:   relative_i -= 3 * edge; relative_j -= edge; break;
			case FACE_TOP:    relative_i -= edge; break;
			case FACE_BOTTOM: relative_i -= edge; relative_j -= 2 * edge; break;
			}

			Vec3 on_cube = image_to_cube (relative_i, relative_j, face, edge);
			f32 r        = sqrtf (on_cube.x * on_cube.x + on_cube.y * on_cube.y);
			f32 theta    = atan2f (on_cube.y, on_cube.x);
			f32 phi      = atan2f (on_cube.z, r);
			// Convert polar coordinates (r, theta, phi) to 2d image coordinates
			f32 uf = 2 * edge * (theta + PI) / PI;
			f32 vf = 2 * edge * (PI / 2 - phi) / PI;
			// Source coords
			int u  = (int)uf;
			int v  = (int)vf;
			// Interpolation factor
			f32 ut = uf - u;
			f32 vt = vf - v;
			// Get the 4 pixel values to interpolate
			Color a = in.data[u       % in.width + clamp (v,     0, in.height - 1) * in.width];
			Color b = in.data[(u + 1) % in.width + clamp (v,     0, in.height - 1) * in.width];
			Color c = in.data[u       % in.width + clamp (v + 1, 0, in.height - 1) * in.width];
			Color d = in.data[(u + 1) % in.width + clamp (v + 1, 0, in.height - 1) * in.width];
			// Interpolate
			Color result = interpolate_colors (a, b, c, d, ut, vt);

			out.data[i + (j + offset_from_top) * out.width] = result;
		}
	}
}

typedef struct Convert_Params
{
	Image in;
	Image out;
	Face  face;
} Convert_Params;

typedef struct Image_Params
{
	u8 *filename;
	u8 *output_folder;
} Image_Params;

internal
bool load_image (const u8 *filename, Image *out)
{
	int width, height, channel_count;
	Color *data = (Color *)stbi_load (filename, &width, &height, &channel_count, 4);
	if (!data) return false;

	out->data     = data;
	out->width    = width;
	out->height   = height;

	return true;
}

internal
u8 *make_out_filename (const u8 *filename, const u8 *output_folder)
{
	// Strip extension
	int name_length = strlen (filename);
	const u8 *name        = filename + name_length;
	while (name > filename)
	{
		name        -= 1;
		name_length -= 1;
		if (*name == '.' || *name == '/' || *name == '\\') break;
	}

	// Separate filename
	while (name > filename)
	{
		name        -= 1;
		if (*name == '/' || *name == '\\') break;
	}
	name_length -= name - filename;

	// Allocate
	int folder_length = strlen (output_folder);
	u8 *result = (u8 *)malloc (folder_length + 1 + name_length + strlen (".png") + 1);
	// Append output_folder/filename.extension
	strcpy (result, output_folder);
	result[folder_length] = '/';
	memcpy (result + folder_length + 1, name, name_length);
	strcpy (result + folder_length + 1 + name_length, ".png");
	result[folder_length + 1 + name_length + strlen (".png")] = 0;

	return result;
}

internal
DWORD face_thread_entry (LPVOID param)
{
	Convert_Params params = *(Convert_Params *)param;
	convert_face (params.in, params.out, params.face);

	return 0;
}

internal
DWORD image_thread_entry (LPVOID param)
{
	Convert_Params convert_params[6];
	DWORD          face_thread_ids[6];
	HANDLE         face_threads[6];

	Image_Params params = *(Image_Params *)param;

	println ("Converting image '%s'.", params.filename);

	Image in;
	bool loaded = load_image (params.filename, &in);
	if (!loaded)
	{
		println ("Could not load image '%s'.", params.filename);

		return 1;
	}

	u8 *out_filename = make_out_filename (params.filename, params.output_folder);
	Image out;
	out.width  = in.width;
	out.height = in.width;
	out.data   = (Color *)calloc (out.width * out.height, sizeof (Color));

	for_range (i, 0, 5)
	{
		convert_params[i].in   = in;
		convert_params[i].out  = out;
		convert_params[i].face = i;
		// @Speed (stefan): We could use a thread pool here.
		face_threads[i] = CreateThread (NULL, 0, face_thread_entry, &convert_params[i], 0, &face_thread_ids[i]);

		if (!face_threads[i])
		{
			println ("Could not create face thread.");

			return 3;
		}
	}

	WaitForMultipleObjects (6, face_threads, TRUE, INFINITE);

	for_range (i, 0, 5)
	{
		CloseHandle (face_threads[i]);
	}

	bool ok = stbi_write_png (out_filename, out.width, out.height, 4, out.data, out.width * 4);
	
	if (!ok)
	{
		println ("Could not write image '%s'.", out_filename);

		free (out_filename);
		free (out.data);
		stbi_image_free (in.data);

		return 1;
	}

	println ("Converted equirectangular image '%s' to spherical cube image '%s'.", params.filename, out_filename);

	free (out_filename);
	free (out.data);
	stbi_image_free (in.data);

	return 0;
}

internal
void print_usage ()
{
	println ("Usage: gen_cubemap input_file0 input_file1 ... input_filen output_folder");
}

int main (int arg_count, u8 **args)
{
	if (arg_count < 2)
	{
		println ("No input filename provided.");
		print_usage ();

		return 0;
	}

	if (arg_count < 3)
	{
		println ("No output folder provided.");
		print_usage ();

		return 0;
	}

	Image_Params   image_params[MAX_PARALLEL_CONVERSIONS];
	DWORD          image_thread_ids[MAX_PARALLEL_CONVERSIONS];
	HANDLE         image_threads[MAX_PARALLEL_CONVERSIONS];

	u8 **input_filenames  = args + 1;
	u8  *output_folder    = args[arg_count - 1];
	int images_to_convert = arg_count - 2;
	int batch_count = images_to_convert / MAX_PARALLEL_CONVERSIONS;
	if (images_to_convert % MAX_PARALLEL_CONVERSIONS != 0) batch_count += 1;

	int converted_images = 0;
	for_range (i, 0, batch_count - 1)
	{
		int image_threads_started = 0;
		// Start all image threads
		for_range (j, 0, MAX_PARALLEL_CONVERSIONS - 1)
		{
			if (converted_images == images_to_convert) break;

			Image_Params *image   = &image_params[j];
			image->filename       = input_filenames[converted_images];
			image->output_folder  = output_folder;

			// @Speed (stefan): We could use a thread pool here.
			image_threads[j] = CreateThread (NULL, 0, image_thread_entry, image, 0, &image_thread_ids[j]);

			converted_images += 1;
			image_threads_started += 1;
		}
		
		WaitForMultipleObjects (image_threads_started, image_threads, TRUE, INFINITE);
	
		for_range (j, 0, MAX_PARALLEL_CONVERSIONS - 1)
		{
			CloseHandle (image_threads[j]);
		}
	}

	return 0;
}
