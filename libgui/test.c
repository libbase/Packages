#include <libbase.h>

#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <GLFW/glfw3.h>
#include "stb_easy_font.h"

typedef struct {
    int         width, height;
    GLFWwindow  *main;
    void        (*draw)();
    thread      thread;
} WindowInstance;

typedef WindowInstance *wi_t;

typedef enum {
    eax = 0xB8,
    ecx = 0xB9,
    edx = 0xBA,
    ebx = 0xBB,
    esp = 0xBC,
    ebp = 0xBD,
    esi = 0xBE,
    edi = 0xBF
} mov;

typedef struct {
    u8 opcode;
    const string x86;
    const string x64;
} reg_info;

#define REG_COUNT 8
reg_info REGISTERS[] = {
    { eax, "eax", "rax" },
    { ebx, "ebx", "rbx" },
    { ecx, "ecx", "rcx" },
    { edx, "edx", "rdx" },
    { ebp, "ebp", "rbp" },
    { esp, "esp", "rsp" },
    { edi, "edi", "rdi" },
    { esi, "esi", "rsi" }
};

mov is_reg_valid(u8 reg)
{
	for(int i = 0; i < REG_COUNT; i++)
	{
		if(REGISTERS[i].opcode == reg || REGISTERS[i].opcode == reg)
			return 1;
	}

	return 0;
}

mov reg_to_type(string reg)
{
    for(int i = 0; i < REG_COUNT; i++)
    {
        if(str_cmp(REGISTERS[i].x86, reg) || str_cmp(REGISTERS[i].x64, reg))
            return REGISTERS[i].opcode;
    }

    return -1;
}

int BUFFERS = 0;

typedef enum {
    x = 0,
    x86 = 0x32,
    x86_64 = 0x64
} arch_t;

bool is_file_lb(u8 *binary)
{ return (binary[0] == 'L' && binary[1] == 'B'); }

bool is_file_x86(u8 arch)
{ return (arch == 0x32 ? 0 : (arch == 0x64 ? 1 : -1)); }

bool is_file_executable(u8 *type)
{
    return (type[0] == 'E' && type[1] == 'X' && type[2] == 'E') ? 0 : 
            (type[0] == 'D' && type[1] == 'Y' && type[2] == 'M') ? 1 : -1; 
}

typedef struct 
{
    string      filename;
    fd_t        file;
    i32         size;
    u8          *buffer;
    u8          FILE_TYPE[2];
    arch_t      ARCH;
    u8          BUFFER_SEGMENT;
    u8          *OPCODE;
    i32         CODE_COUNT;
    i32         BUFFER_SEG;
    u8          **STRINGS;
    i32         *OFFSETS;
    map_t       memory_map;
} binary_t;

#define LB_TYPE_OFFSET 2
#define ARCH_OFFSET 5
#define CODE_SEGMENT_OFFSET 6

binary_t *init_lb(string filename)
{
    if(!filename)
        return NULL;

    binary_t *b = allocate(0, sizeof(binary_t));

    b->filename = filename;
    b->file = open_file(filename, 0, 0);
    if(!b->file)
        lb_panic("Unable to open binary file...!");

    b->size = file_content_size(b->file);
    if(b->size <= 0)
        lb_panic("Unable to reach EOF...!");
    b->buffer = allocate(0, b->size);
    __syscall__(b->file, (long)b->buffer, b->size, -1, -1, -1, _SYS_READ);

	print_args((string []){"File: ", filename, " -> Size: ", NULL}), _printi(b->size), println(NULL);
    file_close(b->file);
    return b;
}

fn validate_file(binary_t *b) {
    int lb, type, arch;
    if(!(lb = is_file_lb(b->buffer)))
        println("Not a LB FILE!");

    print_args((string []){"Is LB binary: ", lb ? "Yes" : "No", "\n", NULL});

    if((type = is_file_executable(b->buffer + LB_TYPE_OFFSET)) == -1)
        lb_panic("Unable to detect lb file type");

    print_args((string []){"File Type: ", type == 0 ? "Executable" : type ? "Shared Lib" : "None", "\n", NULL});



	b->ARCH = (b->buffer + 5)[0];
    if(is_file_x86(b->ARCH) > -1)
        print("Is a "), print(!b->ARCH ? "32-bit" : "64-bit"), println(" file");
}

fn parse_file(binary_t *b)
{
    long ret = __syscall__(0, _HEAP_PAGE_, PROT_READ | PROT_EXEC, 0x02 | 0x20 | 0x40 , -1, 0, 9);
    if(ret < 0)
        lb_panic("failed to create execution buffer!");

    // if ((uintptr_t)ret > 0xFFFFFFFF)
    //     lb_panic("MAP_32BIT did not give 32-bit address");

    b->OPCODE = (u8 *)ret;
    b->CODE_COUNT = 0;
    __syscall__((long)b->OPCODE, _HEAP_PAGE_, PROT_READ | PROT_WRITE | PROT_EXEC, 0, 0, 0, _SYS_MPROTECT);
    for(int i = 6; i < b->size; i++)
    {
        if(b->buffer[i] == 0xFF && b->buffer[i + 1] == 0x00 && b->buffer[i + 2] == 0xFF) {
            b->BUFFER_SEGMENT = i + 3;
            break;
        }
        
        if(__LB_DEBUG__) {
            char buff[3];
            byte_to_hex(((char *)b->buffer)[i], buff);
            _printf("%s, ", buff);
        }

        b->OPCODE[b->CODE_COUNT++] = b->buffer[i];
    }
}

fn parse_buffers(binary_t *b)
{
    b->STRINGS = allocate(0, sizeof(u8 *));
    u8 nbuff[1024];
    int bidx = 0, offsets = 0;
    for(int i = b->BUFFER_SEGMENT, n = 0; i < b->size; i++)
    {
        if(b->buffer[i] == 0x00 && b->buffer[i + 1] == 0xFF)
            { i+= 2; continue; }

        if(b->buffer[i] == 0x00) {
            if(__LB_DEBUG__) _printf(" -> %s\n", nbuff)
            b->STRINGS[bidx++] = to_heap(nbuff, n);
            b->STRINGS = reallocate(b->STRINGS, sizeof(u8 *) * (bidx + 1));
            b->STRINGS[bidx] = NULL;
            n = 0;
            i += 2;
            continue;
        }

        if(is_ascii(b->buffer[i]) && b->buffer[i + 1] == 0xFF) {
            if(__LB_DEBUG__) println("\nSIZE DETECTED");
            i++;
            continue;
        }

        nbuff[n++] = b->buffer[i];
        nbuff[n] = '\0';

        if(__LB_DEBUG__) {
            char buff[3];
            byte_to_hex(((char *)b->buffer)[i], buff);
            
            _printf(b->buffer[i + 1] != 0x00 ? "%s, " : "%s", buff);
        }
    }
}

fn search_n_replace_pointers(binary_t *b)
{
	char byte[3];
    for(int i = 0, nl = 0, strs = 0; i < b->CODE_COUNT; i++)
    {
        if(i == nl + 5) {
            if(__LB_DEBUG__) println(NULL);
            nl += 5;
        }

        if(i <= b->CODE_COUNT - 8 && b->OPCODE[i] == 0x69 && b->OPCODE[i+1] == 0x69 && b->OPCODE[i+2] == 0x69 && b->OPCODE[i+3] == 0x69)
        {
            uintptr_t addr = (uintptr_t)b->STRINGS[strs++];
            size_t placeholder_offset = i;
            for(size_t j = 0; j < sizeof(uintptr_t); j++) {
                b->OPCODE[placeholder_offset + j] = (addr >> (j * 8)) & 0xFF;
                if(__LB_DEBUG__) {
	                byte_to_hex(b->OPCODE[i], byte);
    	            _printf(j == sizeof(uintptr_t) - 1 ? "%s" : "%s, ", byte);
    	        }
            }

            if(__LB_DEBUG__) _printf(" ; Replacing 8-bit pointer: %p -> %s", (void*)addr, b->STRINGS[strs - 1]);

            i += sizeof(uintptr_t) - 1;
            nl += 10;
            continue;
        }

		if(__LB_DEBUG__) {
    	    byte_to_hex(b->OPCODE[i], byte);
	        _printf(i == nl - 5 ? "%s" : "%s, ", byte);
	    }
    }
}

wi_t init_window_instance(int w, int h, char *title)
{
    if(!glfwInit())
        return NULL;

    wi_t window = (wi_t)malloc(sizeof(WindowInstance));
    window->width = w, window->height = h;
    window->main = glfwCreateWindow(w, h, title, NULL, NULL);
    if(!window->main)
        return NULL;

    glfwMakeContextCurrent(window->main);

    return window;
}

void set_resize_handler(wi_t wi, void (*handle)())
{
    glfwSetFramebufferSizeCallback(wi->main, handle);
}

void display_window(wi_t wi)
{
    
    while(!glfwWindowShouldClose(wi->main))
    {
        glClear(GL_COLOR_BUFFER_BIT);


        wi->draw(wi);

        glfwSwapBuffers(wi->main);
        glfwPollEvents();
        usleep(16000);
    }
}

void window_destruct(wi_t wi)
{
    glfwDestroyWindow(wi->main);
    glfwTerminate();
}

void draw_rounded_box(float cx, float cy, float w, float h, float r, int segments, float rgb[3])
{
    glColor3f(rgb[0], rgb[1], rgb[2]);

    glBegin(GL_QUADS);
        glVertex2f(cx - w/2 + r, cy - h/2);
        glVertex2f(cx + w/2 - r, cy - h/2);
        glVertex2f(cx + w/2 - r, cy + h/2);
        glVertex2f(cx - w/2 + r, cy + h/2);

        glVertex2f(cx - w/2, cy - h/2 + r);
        glVertex2f(cx - w/2 + r, cy - h/2 + r);
        glVertex2f(cx - w/2 + r, cy + h/2 - r);
        glVertex2f(cx - w/2, cy + h/2 - r);

        glVertex2f(cx + w/2 - r, cy - h/2 + r);
        glVertex2f(cx + w/2, cy - h/2 + r);
        glVertex2f(cx + w/2, cy + h/2 - r);
        glVertex2f(cx + w/2 - r, cy + h/2 - r);
    glEnd();

    float startAngles[4] = {3.14159265f, 1.5f*3.14159265f, 0.5f*3.14159265f, 0.0f};

    for (int i = 0; i < 4; i++)
    {
        float centerX = (i%2==0) ? cx - w/2 + r : cx + w/2 - r;
        float centerY = (i<2) ? cy - h/2 + r : cy + h/2 - r;

        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(centerX, centerY);

        for (int j = 0; j <= segments; j++)
        {
            float theta = startAngles[i] + (3.14159265f/2.0f) * j / (float)segments;
            float x = centerX + r * cosf(theta);
            float y = centerY + r * sinf(theta);
            glVertex2f(x, y);
        }
        glEnd();
    }
}

void draw_text(wi_t wi, int pos_col, int pos_height, char *title, float scale, float r, float b, float g)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, wi->width, wi->height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    if(!scale)
        scale = 2.0f;

    glPushMatrix();
    glScalef(scale, scale, 1.0f);
    glTranslatef(pos_col, pos_height, 0);
    glColor3f(r, g, b);

    char buffer[99999];
    int num_quads = stb_easy_font_print(0, 0, title, NULL, buffer, sizeof(buffer));

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 16, buffer);
    glDrawArrays(GL_QUADS, 0, num_quads*4);
    glDisableClientState(GL_VERTEX_ARRAY);

    glPopMatrix();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}


fn display_debug(wi_t wi, binary_t *b, int s)
{
	// println("\x1b[32m# ~ [ !CODE SEGMENT! ] ~ #\x1b[39m");
	char byte[3]; int bytes = 0;
    char BUFF[1024] = {0}, BUFFER[1024] = {0};

	for(int i = 0; i < b->CODE_COUNT; i++)
	{
		if(is_reg_valid(b->OPCODE[i]) || (i + 1 < b->CODE_COUNT && b->OPCODE[i] == 0x0F && b->OPCODE[i + 1] == 0x05) || b->OPCODE[i] == 0xC3) {
            string num = int_to_str(bytes);
            str_append_array(BUFFER, (ptr []){
                "Bytes: ", num, " | ", BUFF, "\n", NULL
            });
            memzero(BUFF, 1024);
            pfree(num, 1);

			bytes = 0;
		}
        
		bytes++;
		byte_to_hex(b->OPCODE[i], byte);
        str_append_array(BUFF, (ptr []){
            byte, i == b->CODE_COUNT - 1 ? " " : ", ", NULL
        });
		if(b->OPCODE[i] == 0xC3)
			break;
	}

	draw_text(wi, 16.5f, 105, BUFFER, 1.9f, 0.0f, 0.0f, 0.0f);
	memzero(BUFFER, 1024);

	if(!s) return;
	// println("\n\x1b[32m# ~ [ !DATA SEGMENT! ] ~ #\x1b[39m");
    for(int i = 0 ; i < 2; i++) {
    	int sz = __get_size__(b->STRINGS[i]) - 1;
    	str_append(BUFFER, "Bytes: ");
    	str_append_int(BUFFER, sz);
    	str_append_array(BUFFER, (ptr []){" -> ", b->STRINGS[i], "\t: ", NULL});
        
    	for(int c = 0; b->STRINGS[i][c] != '\0'; c++)
    	{
			byte_to_hex(b->STRINGS[i][c], byte);
			str_append_array(BUFFER, (ptr []){byte, b->STRINGS[i][c] == 0x0A ? " " : ", ", NULL});
    	}
		str_append(BUFFER, "\n");
    }

    draw_text(wi, 300, 105, BUFFER, 2.0f, 0.0f, 0.0f, 0.0f);
}

binary_t *beep;
void draw_window(wi_t wi)
{
    glColor3f(0.2f, 0.6f, 0.9f);
    draw_rounded_box(
        -0.493f,
        -0.15f,
        0.95f,
        1.5f,
        0.1f,
        18,
        (float [3]){1.0f, 1.0f, 1.0f}
    );

    draw_rounded_box(
        -0.48f,
        0.48f,
        0.5f,
        0.2f,
        0.1f,
        18,
        (float [3]){1.0f, 0.0f, 0.0f}
    );
    draw_text(wi, 85, 75, "Disassembled View", 2.0f, 1.0f, 1.0f, 1.0f);

	draw_rounded_box(
		0.493f,
		-0.15f,
		0.95f,
		1.5f,
		0.1f,
		18,
		(float [3]){1.0f, 1.0f, 1.0f}
	);

    draw_rounded_box(
    	0.50f,
    	0.48f,
    	0.5f,
    	0.2f,
    	0.1f,
    	18,
    	(float [3]){1.0f, 0.0f, 0.0f}
    );
    draw_text(wi, 355, 75, "Strings", 2.0f, 1.0f, 1.0f, 1.0f);

    draw_rounded_box(
        0.0f,
        0.80f,
        1.5f,
        0.3f,
        0.1f,
        6,
        (float [3]){1.0f, 0.0f, 0.0f}
    );

    draw_text(wi, 210, 18, "CoreSTD Debugger", 2.0f, 1.0f, 1.0f, 1.0f);
    draw_text(wi, 130, 40, "Home", 2.0f, 1.0f, 1.0f, 1.0f);
    draw_text(wi, 165, 40, "Disassemble Opcode", 2.0f, 0.5f, 0.5f, 0.5f);
    draw_text(wi, 280, 40, "Strings", 2.0f, 0.5f, 0.5f, 0.5f);
    draw_text(wi, 340, 40, "Debug", 2.0f, 0.5f, 0.5f, 0.5f);
	display_debug(wi, beep, 1);
}

int main(void)
{
    init_mem();
    set_heap_sz(_HEAP_PAGE_ * 5);
    println("\x1b[32m# ~ [ !OPENING & VALIDATING BIN! ] ~ #\x1b[39m");
    binary_t *b = init_lb("fag.bin");
    beep = b;
    if(b->size == 0)
        lb_panic("No bytes in file to read...!");

    validate_file(b);
    parse_file(b);
    parse_buffers(b);
    search_n_replace_pointers(b);
    uintptr_t page_start = (uintptr_t)b->OPCODE & ~(_HEAP_PAGE_ - 1);

    wi_t wi = init_window_instance(1000, 600, "CoreSTD Compiler - Debugger");
    wi->draw = draw_window;
    display_window(wi);
    window_destruct(wi);
    return 0;
}

int entry() { return main(); }
