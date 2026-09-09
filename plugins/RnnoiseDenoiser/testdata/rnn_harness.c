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

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		fprintf(stderr, "usage: %s <in.f32> <out.f32> <scale> [stats.csv]\n", argv[0]);
		return 2;
	}
	const float scale = (float)atof(argv[3]);

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

	FILE *fs = NULL;
	if (argc > 4) { fs = fopen(argv[4], "w"); fprintf(fs, "frame,in_rms,out_rms,vad_prob\n"); }

	float frame[480];
	double vsum = 0.0;
	int nf = 0;
	for (long i = 0; i + 480 <= n; i += 480)
	{
		double ein = 0.0;
		for (int j = 0; j < 480; j++)
		{
			frame[j] = in[i + j] * scale;
			ein += (double)in[i + j] * in[i + j];
		}
		float v = rnnoise_process_frame(st, out + i, frame);
		double eout = 0.0;
		for (int j = 0; j < 480; j++)
		{
			out[i + j] /= scale;
			eout += (double)out[i + j] * out[i + j];
		}
		if (fs) fprintf(fs, "%d,%.8g,%.8g,%.6f\n", nf,
			sqrt(ein / 480.0), sqrt(eout / 480.0), v);
		vsum += v;
		nf++;
	}
	if (fs) fclose(fs);
	rnnoise_destroy(st);

	FILE *fo = fopen(argv[2], "wb");
	if (!fo) { perror("out"); return 2; }
	fwrite(out, sizeof(float), (size_t)n, fo);
	fclose(fo);

	printf("frames=%d mean_vad_prob=%.4f\n", nf, vsum / (nf ? nf : 1));
	free(in);
	free(out);
	return 0;
}
