/*
 * rnn_harness.c -- library-level probe for RNNoise (test artifact, not part of the plugin).
 *
 * Reads a raw float32 mono file, pushes it through rnnoise_process_frame() in
 * 480-sample frames at a chosen input scale, writes the (rescaled-back) output
 * and a per-frame CSV of vad_prob + input/output RMS.
 *
 * Build (from lmms-rnnoise/):
 *   gcc -O2 -I plugins/RnnoiseDenoiser/rnnoise -o /tmp/rnn_harness \
 *       testdata/rnn_harness.c \
 *       plugins/RnnoiseDenoiser/rnnoise/{denoise,rnn,pitch,kiss_fft,celt_lpc,nnet,nnet_default,parse_lpcnet_weights,rnnoise_data,rnnoise_tables}.c \
 *       -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rnnoise.h"

/* The input scale: a finite, non-zero number, or 0 when the argument is not one (the output is divided by it). */
static float parse_scale(const char *text)
{
	char *end = NULL;
	const float scale = strtof(text, &end);
	return (end != text && *end == '\0' && isfinite(scale)) ? scale : 0.0f;
}

/* Denoises the `count` samples at `in` (a whole frame, or the zero-padded final part frame) into `out`
 * and writes its stats row; returns the frame's vad probability. */
static float process_frame(DenoiseState *st, const float *in, float *out, int count, float scale, FILE *fs, int nf)
{
	float frame[480] = {0};
	float denoised[480];
	double ein = 0.0;
	for (int j = 0; j < count; j++)
	{
		frame[j] = in[j] * scale;
		ein += (double)in[j] * in[j];
	}
	const float v = rnnoise_process_frame(st, denoised, frame);
	double eout = 0.0;
	for (int j = 0; j < count; j++)
	{
		out[j] = denoised[j] / scale;
		eout += (double)out[j] * out[j];
	}
	if (fs) fprintf(fs, "%d,%.8g,%.8g,%.6f\n", nf, sqrt(ein / count), sqrt(eout / count), v);
	return v;
}

/* Opens the optional stats file and writes its header row. The path is optional, but if it IS
 * given and cannot be written there is nothing to write the per-frame rows into, so this reports
 * the open failure and stops instead of letting the caller fprintf through a null FILE *. */
static FILE *open_stats(const char *path)
{
	FILE *fs = fopen(path, "w");
	if (!fs)
	{
		perror("stats");
		exit(2);
	}
	fprintf(fs, "frame,in_rms,out_rms,vad_prob\n");
	return fs;
}

/* Writes the `count` samples at `samples` to `path`. A short write, a stream error, or a failed
 * close is a failure: buffered data may reach the file only at close, so checking fwrite alone
 * can report success for output that was never stored. */
static int write_output(const char *path, const float *samples, size_t count)
{
	FILE *fo = fopen(path, "wb");
	if (!fo) { perror("out"); return 2; }
	const size_t written = fwrite(samples, sizeof(float), count, fo);
	int write_failed = written != count || ferror(fo);
	if (fclose(fo) != 0) { write_failed = 1; }
	if (write_failed)
	{
		fprintf(stderr, "output write failed\n");
		return 2;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const float scale = argc < 4 ? 0.0f : parse_scale(argv[3]);
	if (scale == 0.0f)
	{
		fprintf(stderr, "usage: %s <in.f32> <out.f32> <scale: finite, non-zero> [stats.csv]\n", argv[0]);
		return 2;
	}

	FILE *fi = fopen(argv[1], "rb");
	if (!fi) { perror("in"); return 2; }
	fseek(fi, 0, SEEK_END);
	long n = ftell(fi) / (long)sizeof(float);
	fseek(fi, 0, SEEK_SET);
	float *in = (float *)malloc((size_t)n * sizeof(float));
	float *out = (float *)malloc((size_t)n * sizeof(float));
	if (!in || !out) { fprintf(stderr, "oom\n"); return 2; }
	if (fread(in, sizeof(float), (size_t)n, fi) != (size_t)n)
	{ fprintf(stderr, "short read\n"); return 2; }
	fclose(fi);
	memset(out, 0, (size_t)n * sizeof(float));

	DenoiseState *st = rnnoise_create(NULL);
	if (!st) { fprintf(stderr, "rnnoise_create failed\n"); return 2; }

	FILE *fs = argc > 4 ? open_stats(argv[4]) : NULL;

	double vsum = 0.0;
	int nf = 0;
	/* A final part frame is zero-padded and processed too, so the output's tail is not left at zero. */
	for (long i = 0; i < n; i += 480)
	{
		const int count = n - i < 480 ? (int)(n - i) : 480;
		vsum += process_frame(st, in + i, out + i, count, scale, fs, nf);
		nf++;
	}
	if (fs) fclose(fs);
	rnnoise_destroy(st);

	if (write_output(argv[2], out, (size_t)n) != 0) { return 2; }

	printf("frames=%d mean_vad_prob=%.4f\n", nf, vsum / (nf ? nf : 1));
	free(in);
	free(out);
	return 0;
}
