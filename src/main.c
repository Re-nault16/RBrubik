#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define FRAME_NS 33000000L
#define CAMERA_DISTANCE 4.2f
#define FACE_SAMPLES 140
#define GAP 0.052f
#define SCRAMBLE_SECONDS 20.0f
#define CYCLE_SECONDS (SCRAMBLE_SECONDS * 2.0f)
#define TRANSITION_SECONDS 4.0f
#define STARTUP_SOLVED_SECONDS SCRAMBLE_SECONDS
#define MAX_RENDER_WIDTH 80
#define MAX_RENDER_PIXEL_HEIGHT 46

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} Color;

typedef struct {
    float x;
    float y;
    float z;
} Vec3;

typedef struct {
    float depth;
    Color color;
    bool filled;
} Pixel;

static volatile sig_atomic_t running = 1;
static struct termios original_termios;
static int original_flags = -1;
static bool terminal_configured = false;

static const Color face_colors[6] = {
    {230, 30, 42},
    {255, 126, 0},
    {0, 82, 180},
    {0, 155, 72},
    {255, 213, 0},
    {245, 245, 245},
};

static const Color gap_color = {18, 18, 18};
static const Color bg_color = {0, 0, 0};

static const int scrambled_stickers[6][9] = {
    {4, 2, 5, 1, 0, 3, 2, 4, 1},
    {3, 5, 0, 4, 1, 2, 5, 3, 0},
    {1, 0, 4, 5, 2, 3, 0, 1, 5},
    {5, 4, 1, 0, 3, 2, 4, 5, 0},
    {2, 3, 0, 1, 4, 5, 3, 2, 1},
    {0, 1, 3, 2, 5, 4, 1, 0, 3},
};

static void handle_signal(int signo) {
    (void)signo;
    running = 0;
}

static void restore_terminal(void) {
    if (!terminal_configured) {
        return;
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    if (original_flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, original_flags);
    }
    printf("\033[?7h\033[?25h\033[?1049l\033[0m");
    fflush(stdout);
    terminal_configured = false;
}

static int configure_terminal(void) {
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        return -1;
    }

    raw = original_termios;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return -1;
    }

    original_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (original_flags == -1) {
        return -1;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, original_flags | O_NONBLOCK) == -1) {
        return -1;
    }

    terminal_configured = true;
    atexit(restore_terminal);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    printf("\033[?1049h\033[?25l\033[?7l\033[2J");
    fflush(stdout);

    return 0;
}

static void get_terminal_size(int *width, int *height) {
    struct winsize ws;

    *width = 80;
    *height = 24;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *width = ws.ws_col;
        *height = ws.ws_row;
    }
}

static Vec3 rotate_vec(Vec3 point, float ax, float ay, float az) {
    float sx = sinf(ax);
    float cx = cosf(ax);
    float sy = sinf(ay);
    float cy = cosf(ay);
    float sz = sinf(az);
    float cz = cosf(az);
    Vec3 out = point;
    float y;
    float z;
    float x;

    y = out.y * cx - out.z * sx;
    z = out.y * sx + out.z * cx;
    out.y = y;
    out.z = z;

    x = out.x * cy + out.z * sy;
    z = -out.x * sy + out.z * cy;
    out.x = x;
    out.z = z;

    x = out.x * cz - out.y * sz;
    y = out.x * sz + out.y * cz;
    out.x = x;
    out.y = y;

    return out;
}

static float clampf(float value, float min, float max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static float smoothstep(float value) {
    value = clampf(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

static float animation_time(float t) {
    if (t < STARTUP_SOLVED_SECONDS) {
        return 0.0f;
    }

    return t - STARTUP_SOLVED_SECONDS;
}

static Color mix_color(Color a, Color b, float amount) {
    Color out;

    amount = clampf(amount, 0.0f, 1.0f);
    out.r = (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * amount);
    out.g = (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * amount);
    out.b = (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * amount);

    return out;
}

static bool is_gap(float u, float v) {
    const float divisions[2] = {-1.0f / 3.0f, 1.0f / 3.0f};

    if (fabsf(u + 1.0f) < GAP || fabsf(u - 1.0f) < GAP ||
        fabsf(v + 1.0f) < GAP || fabsf(v - 1.0f) < GAP) {
        return true;
    }

    for (size_t i = 0; i < 2; i++) {
        if (fabsf(u - divisions[i]) < GAP || fabsf(v - divisions[i]) < GAP) {
            return true;
        }
    }

    return false;
}

static int sticker_index(float u, float v) {
    int col = (int)((u + 1.0f) * 1.5f);
    int row = (int)((v + 1.0f) * 1.5f);

    if (col < 0) {
        col = 0;
    } else if (col > 2) {
        col = 2;
    }

    if (row < 0) {
        row = 0;
    } else if (row > 2) {
        row = 2;
    }

    return row * 3 + col;
}

static float scramble_amount(float t, int face, int sticker) {
    float phase = fmodf(animation_time(t), CYCLE_SECONDS);
    float delay = ((float)((face * 3 + sticker * 5) % 11)) * 0.07f;
    float usable = TRANSITION_SECONDS - 0.8f;
    float amount;

    if (phase < SCRAMBLE_SECONDS) {
        amount = (phase - delay) / usable;
    } else {
        amount = 1.0f - ((phase - SCRAMBLE_SECONDS - delay) / usable);
    }

    return smoothstep(amount);
}

static Color sticker_color(int face, float u, float v, float t) {
    int sticker = sticker_index(u, v);
    Color solved = face_colors[face];
    Color scrambled = face_colors[scrambled_stickers[face][sticker]];
    float amount = scramble_amount(t, face, sticker);
    float transition = 4.0f * amount * (1.0f - amount);
    float shimmer = 0.08f * transition * sinf(t * 7.2f + (float)(face * 13 + sticker * 5));

    return mix_color(solved, scrambled, clampf(amount + shimmer, 0.0f, 1.0f));
}

static Vec3 face_point(int face, float u, float v) {
    switch (face) {
    case 0:
        return (Vec3){u, v, 1.0f};
    case 1:
        return (Vec3){-u, v, -1.0f};
    case 2:
        return (Vec3){1.0f, v, -u};
    case 3:
        return (Vec3){-1.0f, v, u};
    case 4:
        return (Vec3){u, 1.0f, -v};
    default:
        return (Vec3){u, -1.0f, v};
    }
}

static void clear_buffer(Pixel *buffer, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        buffer[i].depth = -1000.0f;
        buffer[i].color = bg_color;
        buffer[i].filled = false;
    }
}

static void plot(Pixel *buffer, int width, int height, int x, int y, float depth, Color color) {
    int index;

    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }

    index = y * width + x;
    if (!buffer[index].filled || depth > buffer[index].depth) {
        buffer[index].depth = depth;
        buffer[index].color = color;
        buffer[index].filled = true;
    }
}

static void render_cube(Pixel *buffer, int width, int height, float t) {
    float ax = 0.55f + sinf(t * 0.37f) * 0.16f;
    float ay = t * 0.92f;
    float az = t * 0.31f;
    float scale = fminf((float)width * 0.52f, (float)height * 1.15f);

    clear_buffer(buffer, width, height);

    for (int face = 0; face < 6; face++) {
        for (int iy = 0; iy < FACE_SAMPLES; iy++) {
            for (int ix = 0; ix < FACE_SAMPLES; ix++) {
                float u = -1.0f + 2.0f * ((float)ix + 0.5f) / (float)FACE_SAMPLES;
                float v = -1.0f + 2.0f * ((float)iy + 0.5f) / (float)FACE_SAMPLES;
                Vec3 rotated = rotate_vec(face_point(face, u, v), ax, ay, az);
                float projected_z = rotated.z + CAMERA_DISTANCE;
                int screen_x;
                int screen_y;
                Color color = is_gap(u, v) ? gap_color : sticker_color(face, u, v, t);

                if (projected_z <= 0.1f) {
                    continue;
                }

                screen_x = (int)((float)width * 0.5f + (rotated.x / projected_z) * scale * 1.24f);
                screen_y = (int)((float)height * 0.5f - (rotated.y / projected_z) * scale * 0.94f);
                plot(buffer, width, height, screen_x, screen_y, -projected_z, color);
            }
        }
    }
}

static int mini(int a, int b) {
    return a < b ? a : b;
}

static void draw_frame(const Pixel *buffer, int width, int height, int terminal_width,
                       int terminal_rows, float t) {
    int frame_rows = (height + 1) / 2;
    int available_rows = terminal_rows > 1 ? terminal_rows - 1 : terminal_rows;
    int x_offset = (terminal_width - width) / 2;
    int y_offset = (available_rows - frame_rows) / 2;
    float phase = fmodf(animation_time(t), CYCLE_SECONDS);
    const char *mode = t < STARTUP_SOLVED_SECONDS ? "solved" :
        phase < SCRAMBLE_SECONDS ? "scramble" : "solving";

    if (x_offset < 0) {
        x_offset = 0;
    }
    if (y_offset < 0) {
        y_offset = 0;
    }

    for (int y = 0; y < height; y += 2) {
        printf("\033[%d;%dH", y_offset + (y / 2) + 1, x_offset + 1);
        for (int x = 0; x < width; x++) {
            const Pixel *upper = &buffer[y * width + x];
            const Pixel *lower = y + 1 < height ? &buffer[(y + 1) * width + x] : NULL;
            bool has_upper = upper->filled;
            bool has_lower = lower != NULL && lower->filled;

            if (has_upper && has_lower) {
                printf("\033[48;2;%u;%u;%um\033[38;2;%u;%u;%um▄",
                       upper->color.r, upper->color.g, upper->color.b,
                       lower->color.r, lower->color.g, lower->color.b);
            } else if (has_upper) {
                printf("\033[49m\033[38;2;%u;%u;%um▀",
                       upper->color.r, upper->color.g, upper->color.b);
            } else if (has_lower) {
                printf("\033[49m\033[38;2;%u;%u;%um▄",
                       lower->color.r, lower->color.g, lower->color.b);
            } else {
                printf("\033[0m ");
            }
        }
        printf("\033[0m");
    }
    printf("\033[%d;1H\033[2K\033[38;2;210;216;230m %s | q to quit \033[0m",
           terminal_rows, mode);
    fflush(stdout);
}

static bool should_quit(void) {
    char input[16];
    ssize_t count = read(STDIN_FILENO, input, sizeof(input));

    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return false;
    }
    if (count <= 0) {
        return false;
    }

    for (ssize_t i = 0; i < count; i++) {
        if (input[i] == 'q' || input[i] == 'Q' || input[i] == 3) {
            return true;
        }
    }

    return false;
}

static double monotonic_seconds(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int main(void) {
    int width;
    int height;
    Pixel *buffer = NULL;
    size_t buffer_size = 0;
    double start_time;

    if (configure_terminal() == -1) {
        fprintf(stderr, "RBrubik: unable to configure terminal: %s\n", strerror(errno));
        return 1;
    }

    start_time = monotonic_seconds();

    while (running) {
        int terminal_width;
        int terminal_rows;
        int render_width;
        int render_height;
        size_t required;
        float elapsed;
        struct timespec delay = {0, FRAME_NS};
        static int previous_terminal_width = 0;
        static int previous_terminal_rows = 0;

        get_terminal_size(&terminal_width, &terminal_rows);
        render_width = mini(terminal_width, MAX_RENDER_WIDTH);
        render_height = terminal_rows > 2 ? (terminal_rows - 1) * 2 : terminal_rows;
        render_height = mini(render_height, MAX_RENDER_PIXEL_HEIGHT);
        required = (size_t)render_width * (size_t)render_height;

        if (required != buffer_size) {
            Pixel *new_buffer = realloc(buffer, required * sizeof(*buffer));
            if (new_buffer == NULL) {
                free(buffer);
                fprintf(stderr, "RBrubik: out of memory\n");
                return 1;
            }
            buffer = new_buffer;
            buffer_size = required;
            printf("\033[2J");
        }

        if (terminal_width != previous_terminal_width || terminal_rows != previous_terminal_rows) {
            printf("\033[2J");
            previous_terminal_width = terminal_width;
            previous_terminal_rows = terminal_rows;
        }

        width = render_width;
        height = render_height;

        elapsed = (float)(monotonic_seconds() - start_time);
        render_cube(buffer, width, height, elapsed);
        draw_frame(buffer, width, height, terminal_width, terminal_rows, elapsed);

        if (should_quit()) {
            running = 0;
        }

        nanosleep(&delay, NULL);
    }

    free(buffer);
    restore_terminal();
    return 0;
}
