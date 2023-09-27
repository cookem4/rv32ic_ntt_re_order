#include "ntt.h" 

// Comparison function for qsort
int compare(const void *a, const void *b) {
    // Cast the void pointers to the correct data type (int*)
    int int_a = *((int*)a);
    int int_b = *((int*)b);

    // Compare the integers
    return int_b - int_a;
}

// Function to sort an array using qsort
void sort(int *x, size_t size) {
    qsort(x, size, sizeof(int), compare);
}

int clog2(long int in) {
    int count = 0;
    in--; // For powers of 2
    while (in > 0) {
        in >>= 1;
        count++;
    }
    return count;
}

int a_pow_b_mod_m(int a, int b, int m) {
    int abm = 1;
    for (int i = 0; i < b; i++) {
        abm = (abm * a) % m;
    }
    return abm;
}

int is_prime(int n) {
    if ((n & 1) == 0) {
        return 0;
    }
    int d = 3;
    while (d * d <= n) {
        if (n % d == 0)
            return 0;
        d = d + 2;
    }
    return n == 1 ? 0 : 1;
}

int * get_prime_factors(int n, int *prime_factor_size) {
    // Max number of prime factors is 2^^clog2(n)
    int ret_idx = 0;
    int *ret = calloc(clog2(n), sizeof(int));
    int fact = 2;
    while (n > 1) {
        if (n % fact == 0) {
            ret[ret_idx] = fact;
            n /= fact;
            fact = 2; // Reset factor
            ret_idx++;
        } else {
            fact++;
        }
    }
    *prime_factor_size = ret_idx;
    // Sort the prime largest to smallest
    sort(ret, (size_t) ret_idx);
    return ret;
}

void get_ntt_params(int n, int * mod, int * g) {
    int k = 2;
    while (1) {
        *mod = n*k + 1;
        if (is_prime(*mod)) {
            break;
        } else {
            k++;
        }
    }
    int good_pow;
    int running_pow;
    for (int i = 2; i < *mod; i++) {
        good_pow = 1;
        running_pow = i;
        for (int j = 2; j <= n; j++) {
            running_pow = (running_pow * i) % *mod;
            if (running_pow == 1 && j != n) {
                good_pow = 0;
                break;
            }
        }
        if (running_pow == 1 && good_pow) {
            *g = i;
            break;
        }
    }
}

int modinv(int a, int m) {
    int q, t, r, newt, newr, tempt, tempr;
    t = 0;
    newt = 1;
    r = m;
    newr = a;
    while (newr != 0) {
        q = r / newr;
        tempt = newt;
        tempr = newr;
        newt = t - q*newt;
        newr = r - q*newr;
        t = tempt;
        r = tempr;
    }
    if (t < 0) {
        t += m;
    }
    return t;
}

int barrett_reduce(int a, ntt_ctx *ctx) {
  long int q, t;
  q = ((long int) a*ctx->barrett_r) >> ctx->barrett_k;
  t = (long int) a - (q * ctx->mod);
  t = t >= (long int) ctx->mod ? t - (long int) ctx->mod : t;
  return (int) t;
}

#if LUT_BASED == 1
static int * g_gen_pows;
static int * g_inv_gen_pows;

void populate_pows_lut(ntt_ctx * fwd_ctx, ntt_ctx * inv_ctx) {
    g_gen_pows = malloc(DIM * sizeof(int));
    g_inv_gen_pows = malloc(DIM * sizeof(int));
    int cur_g = 1;
    int cur_g_inv = 1;
    for (int i = 0; i < DIM; i++) {
        g_gen_pows[i] = cur_g;
        g_inv_gen_pows[i] = cur_g_inv;
        cur_g = barrett_reduce(cur_g * fwd_ctx->w, fwd_ctx);
        cur_g_inv = barrett_reduce(cur_g_inv * inv_ctx->w, inv_ctx);
    }
}
#endif

// Use barrett reduction instead of mod
#if NTT_TYPE == TYPE_N2 && LUT_BASED == 0
void ntt_impl(ntt_ctx * ctx) {
  int *r  = malloc(ctx->size * sizeof(int));
  int *s  = malloc(ctx->size * sizeof(int));
  int twiddle = 1;
  int twiddle_fact = 1; // Geometrically increasing factor in each loop iteration
  for (int i = 0; i < ctx->size; i++) {
      r[i] = ctx->in_seq[i];
      s[i] = 0;
  }

  #if PARALLEL == 1
  #pragma omp parallel for
  #endif
  for (int i = 0; i < ctx->size; i++) {
      twiddle = 1;
      for (int j = 0; j < ctx->size; j++) {
          twiddle = j == 0 ? 1 : barrett_reduce(twiddle * twiddle_fact, ctx);
          s[i] = barrett_reduce(s[i] + r[j] * twiddle, ctx);
      }
      // Increase twiddle factor
      twiddle_fact = barrett_reduce(twiddle_fact * ctx->w, ctx);
  }
  ctx->out_seq = s;
}
#endif

// Lookup powers instead of using a_pow_b_mod_m
#if NTT_TYPE == TYPE_N2 && LUT_BASED == 1
void ntt_impl(ntt_ctx * ctx) {
  int *r  = malloc(ctx->size * sizeof(int));
  int *s  = malloc(ctx->size * sizeof(int));
  for (int i = 0; i < ctx->size; i++) {
      r[i] = ctx->in_seq[i];
      s[i] = 0;
  }
  #if PARALLEL == 1
  #pragma omp parallel for
  #endif
  for (int i = 0; i < ctx->size; i++) {
      for (int j = 0; j < ctx->size; j++) {
          s[i] = barrett_reduce(s[i] + r[j] * (ctx->is_fwd ? g_gen_pows[i*j % ctx->size] : g_inv_gen_pows[i*j % ctx->size]), ctx);
      }
  }
  ctx->out_seq = s;
}
#endif

// Fixed radix only
#if NTT_TYPE == TYPE_FAST && FAST_FIXED == 1
void ntt_impl(ntt_ctx * ctx) {
    int *x = ctx->in_seq;
    int t1, t2;
    int twiddle1, twiddle2;
    int cur_size;
    int cur_idx;
#if LUT_BASED == 0
    int half_rot = a_pow_b_mod_m(ctx->w, ctx->size/2, ctx->mod);
#else // LUT
    int half_rot = ctx->is_fwd ? g_gen_pows[ctx->size/2] : g_inv_gen_pows[ctx->size/2];
#endif
    for (int stride = ctx->size>>1; stride >= 1; stride >>= 1) { // log n
        cur_size = stride<<1;
        // For each of the "sub-transforms" in the CT butterfly
        for (int sub_trans_idx = 0; sub_trans_idx < ctx->size/cur_size; sub_trans_idx++) {
            // We take steps within our sub transform
            for (int step = 0; step < stride; step++) {
                cur_idx = sub_trans_idx*cur_size + step;
#if LUT_BASED == 0
                twiddle1 = a_pow_b_mod_m(ctx->w, sub_trans_idx*(stride) % ctx->size, ctx->mod);
                // Twiddle 2 is offset half a rotation from twiddle 1
                twiddle2 = barrett_reduce(twiddle1 * half_rot, ctx);
#else // LUT
                twiddle1 = ctx->is_fwd ? g_gen_pows[(sub_trans_idx*stride) % ctx->size] : g_inv_gen_pows[(sub_trans_idx*stride) % ctx->size];
                twiddle2 = ctx->is_fwd ? g_gen_pows[(sub_trans_idx*stride + ctx->size/2) % ctx->size] : g_inv_gen_pows[(sub_trans_idx*stride + ctx->size/2) % ctx->size];
#endif
                x[cur_idx + stride] = barrett_reduce(x[cur_idx] + x[cur_idx + stride] * twiddle2, ctx);
                x[cur_idx] = barrett_reduce(x[cur_idx] + x[cur_idx + stride] * twiddle1, ctx);
            }
        }
    }
    ctx->out_seq = x;
}
#endif

#if NTT_TYPE == TYPE_FAST && FAST_MIXED == 1
void ntt_impl(ntt_ctx * ctx) {
    int *x = ctx->in_seq;
    int t;
    int twiddle;
    int n_cur = ctx->size;
    int orig_size = ctx->size;
    int fact_cnt = 0;
    int n2 = n_cur/ctx->prime_factors[fact_cnt];
    int dst_idx;
    // for (int n2 = n_cur/ctx->prime_factors[fact_cnt]; n2 >= 1; n2 /= (ctx->prime_factors[fact_cnt])) { // log n
    while (1) { // Iterates over prime factors. Termination at bottom of loop
        // For each of the "sub-transforms" in the CT butterfly
        for (int n1 = 0; n1 < orig_size/n_cur; n1++) {
            // We take steps within our sub transform
            for (int ni = 0; ni < n2; ni++) {
                int *x_t = calloc(ctx->prime_factors[fact_cnt], sizeof(int));
                for (int butterfly_i = 0; butterfly_i < ctx->prime_factors[fact_cnt]; butterfly_i++) {
                    dst_idx = n1*n_cur + ni + butterfly_i*n2;
                    x_t[butterfly_i] = x[dst_idx]; // Temp save
                }
                // We have N^2 terms to add where N is the prime factor
                // Since N sums into each N node of the sub transform
                for (int butterfly_i = 0; butterfly_i < ctx->prime_factors[fact_cnt]; butterfly_i++) {
                    for (int butterfly_j = 0; butterfly_j < ctx->prime_factors[fact_cnt]; butterfly_j++) {
                        dst_idx = n1*n_cur + ni + butterfly_i*n2;
#if LUT_BASED == 0
                        x[dst_idx] =  barrett_reduce(x[dst_idx] + x_t[butterfly_j] *
                                      a_pow_b_mod_m(ctx->w, ((n_cur/ctx->size)*ni*butterfly_j + (butterfly_i * ctx->size/ctx->prime_factors[fact_cnt]))
                                      % ctx->size, ctx->mod), ctx);
#else // LUT
                        x[dst_idx] =  barrett_reduce(x[dst_idx] + x_t[butterfly_j] *
                                      (ctx->is_fwd ?     g_gen_pows[((n_cur/ctx->size)*ni*butterfly_j + (butterfly_i * ctx->size/ctx->prime_factors[fact_cnt])) % ctx->size]
                                                  : g_inv_gen_pows[((n_cur/ctx->size)*ni*butterfly_j + (butterfly_i * ctx->size/ctx->prime_factors[fact_cnt])) % ctx->size]),
                                      ctx);
#endif
                    }
                }
                free(x_t);
            }
        }
        // Size for next iter
        n_cur = n2;
        fact_cnt++;
        if (fact_cnt >= ctx->prime_factor_size) {
            break;
        }
        n2 /= (ctx->prime_factors[fact_cnt]); // Logn
    }
    ctx->out_seq = x;
}
#endif


int ntt_check(ntt_ctx *fwd_ctx, ntt_ctx *inv_ctx) {

    ntt_impl(fwd_ctx);
    inv_ctx->in_seq = fwd_ctx->out_seq;
    ntt_impl(inv_ctx);
        
    int *orig_arr = fwd_ctx->in_seq;
    int *final_arr = inv_ctx->out_seq;
    int inv_n = modinv(fwd_ctx->size, fwd_ctx->mod);
    for (int i = 0; i < fwd_ctx->size; i++) {
        final_arr[i] = (final_arr[i] * inv_n) % fwd_ctx->mod;
        if (final_arr[i] != orig_arr[i]) {
            return 0;
        }
    }
    return 1;
}