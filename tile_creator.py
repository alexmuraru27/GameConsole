import re
import pygame
import sys
import os

OUTPUT_FILES_DIR = "GeneratedTiles"
DEFAULT_H_FILENAME = "tile"
TILE_SIZE = 16
PIXEL_SIZE = 20
NES_COLS = 16
NES_PALETTE_ROWS = 4
NES_PALETTE_COLOR_SIZE = 20
PALETTE_HEIGHT = NES_PALETTE_ROWS * NES_PALETTE_COLOR_SIZE + 70
WINDOW_WIDTH = TILE_SIZE * PIXEL_SIZE * 2
WINDOW_HEIGHT = TILE_SIZE * PIXEL_SIZE + PALETTE_HEIGHT + 50  # Extra space for controls
BG_COLOR = (255, 255, 255)
FONT_SIZE = 20

MAGIC_FILENAME_REPLACE_PATTERN = "^!!!^"
C_HEADER = f'#ifndef __{MAGIC_FILENAME_REPLACE_PATTERN}_H\n#define __{MAGIC_FILENAME_REPLACE_PATTERN}_H\n#include "tileCreator.h"\n'
EXPORT_ARRAY_BEGINNING = f"{C_HEADER}const uint8_t {MAGIC_FILENAME_REPLACE_PATTERN}_data[64U] = DEFINE_TILE_16(\n\t"
EXPORT_ARRAY_ENDING = ");\n"
EXPORT_ARRAY_SYSTEM_PALETTE_BEGINNING = (
    f"const uint8_t {MAGIC_FILENAME_REPLACE_PATTERN}_palette[4U] = {{"
)
EXPORT_ARRAY_SYSTEM_PALETTE_ENDING = "};\n#endif"

NES_PALETTE = [
    (98, 98, 98),
    (0, 46, 152),
    (12, 17, 194),
    (59, 0, 194),
    (101, 0, 152),
    (125, 0, 78),
    (125, 0, 0),
    (101, 25, 0),
    (59, 54, 0),
    (12, 79, 0),
    (0, 91, 0),
    (0, 89, 0),
    (0, 73, 78),
    (0, 0, 0),
    (0, 0, 0),
    (0, 0, 0),
    (171, 171, 171),
    (0, 100, 243),
    (53, 60, 255),
    (118, 27, 255),
    (174, 10, 243),
    (206, 13, 143),
    (206, 35, 28),
    (174, 71, 0),
    (118, 111, 0),
    (53, 114, 0),
    (0, 161, 0),
    (0, 158, 28),
    (0, 136, 143),
    (0, 0, 0),
    (0, 0, 0),
    (0, 0, 0),
    (255, 255, 255),
    (76, 181, 255),
    (133, 140, 255),
    (200, 107, 255),
    (255, 89, 255),
    (255, 92, 225),
    (255, 115, 107),
    (255, 152, 5),
    (200, 192, 0),
    (133, 226, 0),
    (76, 244, 5),
    (43, 241, 107),
    (43, 218, 225),
    (78, 78, 78),
    (0, 0, 0),
    (0, 0, 0),
    (255, 255, 255),
    (184, 255, 255),
    (206, 209, 255),
    (232, 196, 255),
    (255, 189, 255),
    (255, 190, 243),
    (255, 199, 196),
    (255, 214, 156),
    (232, 230, 132),
    (206, 243, 132),
    (184, 250, 156),
    (171, 249, 196),
    (171, 240, 243),
    (184, 184, 184),
    (0, 0, 0),
    (0, 0, 0),
]

pygame.init()
screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
pygame.display.set_caption("NES Tile")
font = pygame.font.SysFont(None, FONT_SIZE)
clock = pygame.time.Clock()

# grid data
grid = [[0 for _ in range(TILE_SIZE)] for _ in range(TILE_SIZE)]
selected_colors_idx = [32, 10, 5, 2]
active_color_slot = 1

# file name for output
input_box = pygame.Rect(10, WINDOW_HEIGHT - 40, 200, 30)
input_text = DEFAULT_H_FILENAME
text_active = False

BUTTON_WIDTH = 60
BUTTON_HEIGHT = 30
save_button = pygame.Rect(210, WINDOW_HEIGHT - 40, BUTTON_WIDTH, BUTTON_HEIGHT)
load_button = pygame.Rect(260, WINDOW_HEIGHT - 40, BUTTON_WIDTH, BUTTON_HEIGHT)
clear_button = pygame.Rect(
    WINDOW_WIDTH - BUTTON_WIDTH, WINDOW_HEIGHT - 40, BUTTON_WIDTH, BUTTON_HEIGHT
)


def draw_grid():
    offset_x = TILE_SIZE * PIXEL_SIZE
    for y in range(TILE_SIZE):
        for x in range(TILE_SIZE):
            color = NES_PALETTE[selected_colors_idx[grid[y][x]]]
            rect_border = pygame.Rect(
                x * PIXEL_SIZE, y * PIXEL_SIZE + PALETTE_HEIGHT, PIXEL_SIZE, PIXEL_SIZE
            )
            rect_no_border = pygame.Rect(
                offset_x + x * PIXEL_SIZE,
                y * PIXEL_SIZE + PALETTE_HEIGHT,
                PIXEL_SIZE,
                PIXEL_SIZE,
            )
            pygame.draw.rect(screen, color, rect_border)
            pygame.draw.rect(screen, (50, 50, 50), rect_border, 1)
            pygame.draw.rect(screen, color, rect_no_border)


def draw_nes_palette():
    for i, color in enumerate(NES_PALETTE):
        col = i % NES_COLS
        row = i // NES_COLS
        rect = pygame.Rect(
            col * NES_PALETTE_COLOR_SIZE,
            row * NES_PALETTE_COLOR_SIZE,
            NES_PALETTE_COLOR_SIZE,
            NES_PALETTE_COLOR_SIZE,
        )
        pygame.draw.rect(screen, color, rect)
        pygame.draw.rect(screen, (0, 0, 0), rect, 1)


def draw_selected_colors():
    box_size = 50
    margin = 10
    y = NES_PALETTE_ROWS * NES_PALETTE_COLOR_SIZE + 10
    for i, idx in enumerate(selected_colors_idx):
        rect = pygame.Rect(margin + i * (box_size + margin), y, box_size, box_size)
        pygame.draw.rect(screen, NES_PALETTE[idx], rect)
        pygame.draw.rect(screen, (0, 0, 0), rect, 2)
        if i == active_color_slot:
            pygame.draw.rect(screen, (255, 255, 0), rect, 4)


def draw_file_controls():
    # text
    pygame.draw.rect(screen, (255, 255, 255), input_box)
    pygame.draw.rect(screen, (0, 0, 0), input_box, 2)
    txt_surf = font.render(input_text, True, (0, 0, 0))
    screen.blit(txt_surf, (input_box.x + 5, input_box.y + 5))

    # save
    pygame.draw.rect(screen, (180, 255, 180), save_button)
    save_txt = font.render("Save", True, (0, 0, 0))
    screen.blit(save_txt, (save_button.x + 5, save_button.y + 5))

    # load
    pygame.draw.rect(screen, (180, 180, 255), load_button)
    load_txt = font.render("Load", True, (0, 0, 0))
    screen.blit(load_txt, (load_button.x + 5, load_button.y + 5))

    # clear
    pygame.draw.rect(screen, (255, 180, 180), clear_button)
    clear_txt = font.render("Clear", True, (0, 0, 0))
    screen.blit(clear_txt, (clear_button.x + 5, clear_button.y + 5))


def handle_nes_palette_click(pos):
    for i in range(len(NES_PALETTE)):
        col = i % NES_COLS
        row = i // NES_COLS
        rect = pygame.Rect(
            col * NES_PALETTE_COLOR_SIZE,
            row * NES_PALETTE_COLOR_SIZE,
            NES_PALETTE_COLOR_SIZE,
            NES_PALETTE_COLOR_SIZE,
        )
        if rect.collidepoint(pos):
            return i
    return None


def handle_selected_colors_click(pos):
    box_size = 50
    margin = 10
    y = NES_PALETTE_ROWS * NES_PALETTE_COLOR_SIZE + 10
    for i in range(4):
        rect = pygame.Rect(margin + i * (box_size + margin), y, box_size, box_size)
        if rect.collidepoint(pos):
            return i
    return None


def export_as_array(filename):
    raw_filename = filename
    if not filename.endswith(".h"):
        filename += ".h"
    if not os.path.exists(OUTPUT_FILES_DIR):
        os.mkdir(OUTPUT_FILES_DIR)
    full_path = os.path.join(OUTPUT_FILES_DIR, filename)
    with open(full_path, "w") as f:
        # save tile data
        export_array_prologue = EXPORT_ARRAY_BEGINNING.replace(
            MAGIC_FILENAME_REPLACE_PATTERN, raw_filename
        )
        f.write(export_array_prologue)
        flat_list = [val for row in grid for val in row]
        for i, val in enumerate(flat_list):
            f.write(f"{val}")
            if i < len(flat_list) - 1:
                f.write(", ")
                if (i + 1) % TILE_SIZE == 0:
                    f.write("\n\t")
        f.write(EXPORT_ARRAY_ENDING)

        # save system palette color
        export_array_system_palette_prologue = (
            EXPORT_ARRAY_SYSTEM_PALETTE_BEGINNING.replace(
                MAGIC_FILENAME_REPLACE_PATTERN, raw_filename
            )
        )
        f.write(export_array_system_palette_prologue)
        for i, val in enumerate(selected_colors_idx):
            f.write(f"{hex(val)}")
            if i < len(selected_colors_idx) - 1:
                f.write(", ")
        f.write(EXPORT_ARRAY_SYSTEM_PALETTE_ENDING)
    print(f"Saved tile to {full_path}")


def load_from_file(filename):
    raw_filename = filename
    if not filename.endswith(".h"):
        filename += ".h"
    full_path = os.path.join(OUTPUT_FILES_DIR, filename)
    if not os.path.exists(full_path):
        print(f"File {full_path} not found.")
        return
    try:
        with open(full_path, "r") as f:
            restored_content = f.read()

            export_array_prologue = EXPORT_ARRAY_BEGINNING.replace(
                MAGIC_FILENAME_REPLACE_PATTERN, raw_filename
            )
            tile_start = restored_content.find(export_array_prologue) + len(
                export_array_prologue
            )
            tile_end = restored_content.find(EXPORT_ARRAY_ENDING, tile_start)
            tile_raw = (
                restored_content[tile_start:tile_end]
                .replace("\n", "")
                .replace("\t", "")
                .replace(" ", "")
            )
            cleaned_tile_raw = re.sub(r"[^a-zA-Z0-9,]", "", tile_raw)
            tile_list = [int(x) for x in cleaned_tile_raw.split(",") if x != ""]

            # Extract the palette array
            palette_content = restored_content[tile_end:].replace(
                EXPORT_ARRAY_ENDING, ""
            )
            export_array_system_palette_prologue = (
                EXPORT_ARRAY_SYSTEM_PALETTE_BEGINNING.replace(
                    MAGIC_FILENAME_REPLACE_PATTERN, raw_filename
                )
            )
            palette_start = palette_content.find(
                export_array_system_palette_prologue
            ) + len(export_array_system_palette_prologue)
            palette_end = palette_content.find(
                EXPORT_ARRAY_SYSTEM_PALETTE_ENDING, palette_start
            )
            palette_raw = (
                palette_content[palette_start:palette_end]
                .replace("\n", "")
                .replace(" ", "")
            )
            cleaned_palette_raw = re.sub(r"[^a-zA-Z0-9,]", "", palette_raw)
            palette_list = [
                int(x, 16) for x in cleaned_palette_raw.split(",") if x != ""
            ]

            for idx, pix in enumerate(tile_list):
                if idx < len(tile_list):
                    grid[idx // TILE_SIZE][idx % TILE_SIZE] = pix
            for idx, palette_data in enumerate(palette_list):
                selected_colors_idx[idx] = palette_data
        print(f"Loaded tile from {full_path}")
    except Exception as e:
        print("Failed to load:", e)


def clear_screen():
    for i in range(TILE_SIZE):
        for j in range(TILE_SIZE):
            grid[i][j] = 0


running = True
mouse_down = False
load_from_file(DEFAULT_H_FILENAME)

def draw_prompt(question):
    screen.fill((0, 0, 0))
    text = font.render(f"{question} (Y/N)", True, (255, 255, 255))
    screen.blit(text, (40, 80))
    pygame.display.flip()

def ask_yes_no(question):
    draw_prompt(question)
    while True:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                pygame.quit()
                sys.exit()
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_y:
                    return True
                elif event.key == pygame.K_n:
                    return False
while running:
    screen.fill(BG_COLOR)
    draw_nes_palette()
    draw_selected_colors()
    draw_grid()
    draw_file_controls()
    pygame.display.flip()

    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False

        elif event.type == pygame.MOUSEBUTTONDOWN:
            mouse_down = True
            x, y = pygame.mouse.get_pos()
            if input_box.collidepoint((x, y)):
                text_active = True
            else:
                text_active = False
            if save_button.collidepoint((x, y)):
                if ask_yes_no(f"Save \"{input_text}\"?"):
                    export_as_array(input_text)
            elif load_button.collidepoint((x, y)):
                if ask_yes_no(f"Load \"{input_text}\"?"):
                    load_from_file(input_text)
            elif clear_button.collidepoint((x, y)):
                if ask_yes_no(f"Clear \"{input_text}\"?"):
                    clear_screen()
            elif y < NES_PALETTE_ROWS * NES_PALETTE_COLOR_SIZE:
                sel = handle_nes_palette_click((x, y))
                if sel is not None:
                    selected_colors_idx[active_color_slot] = sel
                    print(f"Set slot {active_color_slot} to NES color index {sel}")
            elif NES_PALETTE_ROWS * NES_PALETTE_COLOR_SIZE <= y < PALETTE_HEIGHT:
                sel = handle_selected_colors_click((x, y))
                if sel is not None:
                    active_color_slot = sel
                    print(f"Active drawing slot set to {active_color_slot}")
            else:
                grid_x = x // PIXEL_SIZE
                grid_y = (y - PALETTE_HEIGHT) // PIXEL_SIZE
                if 0 <= grid_x < TILE_SIZE and 0 <= grid_y < TILE_SIZE:
                    if event.button == 1:
                        grid[grid_y][grid_x] = active_color_slot
                    elif event.button == 3:
                        grid[grid_y][grid_x] = 0

        elif event.type == pygame.MOUSEBUTTONUP:
            mouse_down = False
        elif event.type == pygame.MOUSEMOTION and mouse_down:
            x, y = event.pos
            grid_x = x // PIXEL_SIZE
            grid_y = (y - PALETTE_HEIGHT) // PIXEL_SIZE
            if 0 <= grid_x < TILE_SIZE and 0 <= grid_y < TILE_SIZE:
                if pygame.mouse.get_pressed()[0]:
                    grid[grid_y][grid_x] = active_color_slot
                elif pygame.mouse.get_pressed()[2]:
                    grid[grid_y][grid_x] = 0

        elif event.type == pygame.KEYDOWN:
            if event.key == pygame.K_s and pygame.key.get_mods() & pygame.KMOD_LCTRL:
                if ask_yes_no(f"Save \"{input_text}\"?"):
                    export_as_array(input_text)
            elif event.key == pygame.K_l and pygame.key.get_mods() & pygame.KMOD_LCTRL:
                if ask_yes_no(f"Load \"{input_text}\"?"):
                    load_from_file(input_text)
            if text_active:
                if event.key == pygame.K_BACKSPACE:
                    input_text = input_text[:-1]
                elif event.key == pygame.K_RETURN:
                    text_active = False
                else:
                    if event.unicode.isalnum() or event.unicode == '_':
                        input_text += event.unicode
    clock.tick(60)

pygame.quit()
sys.exit()
