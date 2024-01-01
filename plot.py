import argparse
import dataclasses
import sqlite3
from datetime import datetime, timedelta
from multiprocessing import Pool
import os
from pathlib import Path

import numpy
from PIL import Image
import struct
from typing import List

import numpy as np
from tqdm import tqdm
import matplotlib

import warnings
warnings.filterwarnings("ignore")

# matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.transforms import TransformedBbox, Bbox


frame_timing = []
frame_scores = []
user_data = {}
events = []


@dataclasses.dataclass
class Frame:
    frame_time: datetime
    total_score: float
    user_ids: List[int]
    placing: List[float]
    score: List[float]


def load_data(input_filename):
    with open(input_filename, 'rb') as f:
        # Read the header
        frames = []
        num_frames = struct.unpack("Q", f.read(8))[0]

        for _ in range(num_frames):
            raw_time, total_score, num_users = struct.unpack("QdQ", f.read(24))
            fmt_long = "Q" * num_users
            fmt_double = "d" * num_users
            user_ids = list(struct.unpack(fmt_long, f.read(8 * num_users)))
            placing = list(struct.unpack(fmt_double, f.read(8 * num_users)))
            score = list(struct.unpack(fmt_double, f.read(8 * num_users)))

            frames.append(Frame(
                datetime.fromtimestamp(raw_time),
                total_score,
                user_ids,
                placing,
                score
            ))

    return frames


def load_user_data(db_file):
    conn = sqlite3.Connection(db_file)
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM users;")
    return {x[0]: [x[1], x[2]] for x in cursor.fetchall()}


def load_events(event_file):
    loaded_events = []
    with open(event_file, "r") as f:
        lines = f.readlines()

    skipping = False
    for line in lines:
        if line[0] == '#': # Comment
            continue

        if skipping and line.strip() == "":
            skipping = False
            continue

        if not skipping and line.strip() != "":
            words = line.split()
            loaded_events.append([
                datetime.strptime(words[0], "%Y-%m-%d"),
                int(words[1]),
                ' '.join(words[2:])
            ])
            skipping = True

    return loaded_events


def plot_frame(data):
    frame, filename, args = data
    frame: Frame

    fig, (ax, ax2) = plt.subplots(2, 1, height_ratios=[4.5, 1], dpi=args.dpi)
    fig: plt.Figure
    ax: plt.Axes
    ax2: plt.Axes
    fig.patch.set_facecolor('white')
    fig.patch.set_alpha(1.0)
    fig.set_size_inches(16, 9)

    inverse_placing = [args.users_per_frame + 1 - x for x in frame.placing]
    bars = ax.barh(inverse_placing, frame.score, color='#1f77b4', alpha=0.7)
    ax.bar_label(bars, fmt='%.0f')
    ax.set_yticks(inverse_placing, [user_data[x][0] for x in frame.user_ids])

    # Plot avatars
    y_lim_max = max(100.0, frame.score[0] * 1.15 if frame.score != [] else 0)
    av_scale = 0.8
    av_width = y_lim_max / (2.4 * args.users_per_frame) * av_scale
    for (user_id, placing, score) in zip(frame.user_ids, inverse_placing, frame.score):
        ax.imshow(user_data[user_id][2], aspect="auto",
                  extent=(score - av_width * 1.25, score - av_width * 0.25,
                          placing - 0.5 * av_scale, placing + 0.5 * av_scale),
                  zorder=1)
    ax.axvline(1500, color='r', linewidth=1)

    # ax.tick_params(axis='y', which='both', left=False, right=False, labelbottom=False)
    ax.set_title(frame.frame_time, fontsize=24)
    ax.set_xlim(0, y_lim_max)
    ax.set_ylim(0.5, args.users_per_frame + 0.5)

    # Draw the text
    events_possible = [x for x in events if x[0] < frame.frame_time]
    colours = ["#ED5564", "#FFCE54", "#A0D568", "#4FC1E8", "#AC92EB", "#000000"]
    colours.reverse()
    if events_possible:
        event_x_pos = 0.55 * y_lim_max
        event_y_stride = -0.7
        bbox = Bbox([[event_x_pos, 0], [1, event_y_stride * -5]])
        time_till_last_event = frame.frame_time - events_possible[-1][0]
        if time_till_last_event < timedelta(days=1) and len(events_possible) > 1:
            event_y_start = (-5 + time_till_last_event / timedelta(days=1)) * event_y_stride
            events_possible.pop(-1)
        else:
            event_y_start = -5 * event_y_stride
        for i in range(min(5, len(events_possible))):
            event = events_possible[-(i + 1)]
            ax.text(event_x_pos, event_y_start + event_y_stride * i,
                    f"{event[0].strftime('%Y-%m-%d')}: {event[2]}",
                    va='bottom', ha='left', fontsize=14, clip_box=bbox, clip_on=True,
                    color=colours[event[1]])

    ax2.plot(frame_timing, frame_scores)
    ax2.grid(True)
    ax2.axvline(frame.frame_time, color='r', linewidth=1)
    ax2.axhline(frame.total_score, -0.05, 1, color='r', linewidth=1, clip_on=False)
    for event in events:
        ax2.axvline(event[0], 0.95, 1, color=colours[event[1]], linewidth=2)
    ax2.text(frame.frame_time, 1, frame.frame_time.strftime("%Y-%m-%d"), rotation=0,
             transform=ax2.get_xaxis_text1_transform(0)[0], fontsize=11, ha='center', color='r',
             va='bottom')
    ax2.text(-0.05, frame.total_score, f"{frame.total_score:.0f}", rotation=0,
             transform=ax2.get_yaxis_text1_transform(0)[0], fontsize=11, va='center', color='r',
             ha='right')

    fig.savefig(filename)
    plt.close(fig)

    return None


def main():
    # Load input
    parser = argparse.ArgumentParser(
        prog="Activity Graph Generator",
        description="Generate graph"
    )
    parser.add_argument('-i', '--input', default='in.bin')
    parser.add_argument('-o', '--output', default='vid.bin')
    parser.add_argument('--events', default='events.txt')
    parser.add_argument('-d', '--db-file', default='userinfo.db')
    parser.add_argument('-r', '--dpi', default=80, type=int)
    parser.add_argument('-of', '--output-folder', default='imgs_out/')
    parser.add_argument('-s', '--start-frame', default='0', type=int)
    parser.add_argument('-e', '--end-frame', default='-1', type=int)
    parser.add_argument('-u', '--users-per-frame', default='15', type=int)
    parser.add_argument('-t', '--threads', default='24', type=int)

    args = parser.parse_args()
    frames = load_data(args.input)

    # For the total score plot
    global frame_timing, frame_scores, user_data, events
    frame_timing = [frame.frame_time for frame in frames]
    frame_scores = [frame.total_score for frame in frames]
    events = load_events(args.events)

    # For loading avatars
    user_data = load_user_data(args.db_file)
    users_of_interest = set()
    for frame in frames:
        users_of_interest.update(frame.user_ids)
    print("Loading avatars:")
    for user_id in tqdm(users_of_interest):
        if user_id not in user_data:
            print(f"Warning: User {user_id} needs to be plotted but no metadata is loaded for this user.")
            user_data[user_id] = ["Unknown", "default_av.png", plt.imread("default_av.png")]
            continue
        img = Image.open(user_data[user_id][1] if user_data[user_id][1] != "None" else "default_av.png")
        img_np = np.asarray(img.convert("RGB"))
        # img_np = np.asarray(img)
        xx, yy = numpy.mgrid[:img_np.shape[0], :img_np.shape[1]]
        middle = (img_np.shape[0] / 2, img_np.shape[1] / 2)
        radius = middle[0]
        circle = (xx - middle[0]) ** 2 + (yy - middle[0]) ** 2
        transparency_layer = 255 - 255 * (circle > radius ** 2)
        img_np = np.dstack((img_np, transparency_layer))
        # print(user_id, img_np.shape, transparency_layer.shape)
        user_data[user_id].append(img_np)

    # plt.imshow(user_data[789015389018521600][-1]); plt.show(); exit()

    if not os.path.exists(args.output_folder):
        os.makedirs(args.output_folder)

    # Generate frames
    frames = frames[args.start_frame:None if args.end_frame == -1 else args.end_frame]
    print("Loading frames:")
    with Pool(args.threads) as p:
        list(tqdm(p.imap(
            plot_frame,
            [(frame, os.path.join(args.output_folder, f"{i + 1:05}.png"), args) for i, frame in enumerate(frames)]
        ), total=len(frames)))
    # for i, frame in enumerate(tqdm(frames)):
    #     plot_frame(frame, os.path.join(args.output_folder, f"{i+1:05}.png"))

    Path(args.output).touch()


if __name__ == "__main__":
    main()
