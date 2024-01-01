import argparse
import json
import os

from pathlib import Path
import requests
import struct
import sqlite3
import time

from tqdm import tqdm


token = ''

def init_db(conn: sqlite3.Connection):
    cursor = conn.cursor()
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS users (
            user_id INTEGER PRIMARY KEY, 
            username STRING, 
            avatar_file STRING
        ); 
    """)
    conn.commit()


def get_loaded_users(conn: sqlite3.Connection):
    cursor = conn.cursor()
    cursor.execute("""SELECT user_id FROM users;""")
    return set((x[0] for x in cursor.fetchall()))


def load_users_of_interest(input_filename):
    with open(input_filename, 'rb') as f:
        # Read the header
        all_user_ids = set()
        num_frames = struct.unpack("Q", f.read(8))[0]

        for _ in range(num_frames):
            raw_time, total_score, num_users = struct.unpack("QdQ", f.read(24))
            fmt_long = "Q" * num_users
            user_ids = list(struct.unpack(fmt_long, f.read(8 * num_users)))
            f.read(8 * num_users)
            f.read(8 * num_users)

            for user in user_ids:
                all_user_ids.add(user)

    return all_user_ids


def pull_info(user_id, output_folder):
    r = requests.get(f"https://canary.discord.com/api/v10/users/{user_id}", headers={
        "Content-Type": "application/json",
        "Authorization": f"Bot {token}"
    })

    data = r.content.decode('utf8')
    data = json.loads(data)
    while r.status_code != 200:
        if r.status_code == 429:
            time.sleep(data["retry_after"] + 1)
            r = requests.get(f"https://canary.discord.com/api/v10/users/{user_id}", headers={
                "Content-Type": "application/json",
                "Authorization": f"Bot {token}"
            })

            data = r.content.decode('utf8')
            data = json.loads(data)
        else:
            print(user_id, r.status_code, r.content)
            return None

    av_filename = os.path.join(output_folder, f"{user_id}.png")
    if data['avatar'] is not None:
        av_link = f"https://cdn.discordapp.com/avatars/{user_id}/{data['avatar']}"
        av_request = requests.get(av_link)
        with open(av_filename, "wb") as f:
            f.write(av_request.content)

    return data["username"], None if data['avatar'] is None else av_filename


def main():
    # Load input
    parser = argparse.ArgumentParser(
        prog="User Information Loader",
        description="Pull User Information"
    )
    parser.add_argument('-i', '--input', default='in.bin')
    parser.add_argument('-d', '--db-file', default='userinfo.db')
    parser.add_argument('-of', '--output-folder', default='avatars/')
    args = parser.parse_args()

    if not os.path.exists(args.output_folder):
        os.makedirs(args.output_folder)

    # Load already downloaded users
    conn = sqlite3.Connection(args.db_file)
    init_db(conn)
    already_loaded_users = get_loaded_users(conn)

    users_to_pull = load_users_of_interest(args.input)
    # users_to_pull = {281300961312374785}
    users_to_pull = users_to_pull - already_loaded_users
    print(f"Users to pull: {len(users_to_pull)}")
    
    global token
    with open('token.txt') as tfile: 
        token = tfile.read().strip()

    for user_id in tqdm(users_to_pull):
        user_data = pull_info(user_id, args.output_folder)

        cursor = conn.cursor()
        if user_data is not None:
            cursor.execute("""INSERT INTO users VALUES (?, ?, ?);""", [user_id, user_data[0], str(user_data[1])])
        else:
            cursor.execute("""INSERT INTO users VALUES (?, ?, ?);""", [user_id, "Unknown User", "None"])
        conn.commit()

    # Touch the database file (so Make updates the time)
    Path(args.db_file).touch()



if __name__ == "__main__":
    main()
