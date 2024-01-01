
gentbl: gentbl.cpp
	g++ gentbl.cpp -lsqlite3 -Ofast -o gentbl
genframeinfo: genframeinfo.cpp
	g++ genframeinfo.cpp -Ofast -o genframeinfo


tbl2021y.bin: gentbl
	./gentbl -d modsdb-2021-01-07.db -b 31536000 -fh 1 -t 24 -o tbl2021y.bin
tbl2021.bin: gentbl
	./gentbl -d modsdb-2021-01-07.db -b 86400 -fh 30 -t 24 -o tbl2021.bin
tbl2023_11.bin: gentbl
	./gentbl -d modsdb-2023-11-25.db -b 86400 -fh 30 -t 24 -o tbl2023_11.bin
tbl2023_12.bin: gentbl
	./gentbl -d modsdb-2023-12-24.db -b 86400 -fh 30 -t 24 -o tbl2023_12.bin
tbl2023_12h.bin: gentbl
	./gentbl -d modsdb-2023-12-24.db -b 3600 -fh 720 -t 24 -o tbl2023_12h.bin
tbl2023_12_8h.bin: gentbl
	./gentbl -d modsdb-2023-12-24.db -b 28800 -fh 90 -t 23 -o tbl2023_12_8h.bin
tbl2023_12q.bin: gentbl
	./gentbl -d modsdb-2023-12-24.db -b 21600 -fh 120 -t 24 -o tbl2023_12q.bin
tbl2024_01_8h.bin: gentbl modsdb-2024-01-01.db
	./gentbl -d modsdb-2024-01-01.db -b 28800 -fh 90 -t 16 -o tbl2024_01_8h.bin


frm2021.bin: tbl2021.bin genframeinfo
	./genframeinfo -i tbl2021.bin -o frm2021.bin
frm2023_11.bin: tbl2023_11.bin genframeinfo
	./genframeinfo -i tbl2023_11.bin -o frm2023_11.bin
frm2023_12.bin: tbl2023_12.bin genframeinfo
	./genframeinfo -i tbl2023_12.bin -o frm2023_12.bin -f 15
frm2023_12h.bin: tbl2023_12h.bin genframeinfo
	./genframeinfo -i tbl2023_12h.bin -o frm2023_12h.bin
frm2023_12q.bin: tbl2023_12q.bin genframeinfo
	./genframeinfo -i tbl2023_12q.bin -o frm2023_12q.bin -f 7
frm2023_12_8h.bin: tbl2023_12_8h.bin gentbl
	./genframeinfo -i tbl2023_12_8h.bin -o frm2023_12_8h.bin -f 10
frm2023_12_8h_f17.bin: tbl2023_12_8h.bin gentbl
	./genframeinfo -i tbl2023_12_8h.bin -o frm2023_12_8h_f17.bin -f 17
frm2024_01_8h_f17.bin: tbl2024_01_8h.bin gentbl
	./genframeinfo -i tbl2024_01_8h.bin -o frm2024_01_8h_f17.bin -f 17


vid2021.bin: frm2021.bin plot.py userinfo.db
	python3 plot.py -i frm2021.bin -o vid2021.bin -of vid2021/
vid2023_12c.bin: frm2023_12.bin plot.py userinfo.db
	python3 plot.py -i frm2023_12.bin -o vid2023_12c.bin -of vid2023_12c/ -s 10500 -e 11000
vid2023_12.bin: frm2023_12.bin plot.py userinfo.db
	python3 plot.py -i frm2023_12.bin -o vid2023_12.bin -of vid2023_12/
vid2023_12q.bin: frm2023_12q.bin plot.py userinfo.db
	python3 plot.py -i frm2023_12q.bin -o vid2023_12q.bin -of vid2023_12q/ -t 23
vid2023_12qc.bin: frm2023_12q.bin plot.py userinfo.db
	python3 plot.py -i frm2023_12q.bin -o vid2023_12qc.bin -of vid2023_12qc/ -s 21200 -e 21400 --events events.txt
vid2023_12q_4k.bin: frm2023_12q.bin plot.py userinfo.db
	python3 plot.py -i frm2023_12q.bin -o vid2023_12q_4k.bin -of vid2023_12q_4k/ --dpi 240 -t 23
vid2023_12_8h_f17_4k.bin: frm2023_12_8h_f17.bin plot.py userinfo.db
	python3 plot.py -i frm2023_12_8h_f17.bin -o vid2023_12_8h_f17_4k.bin -of vid2023_12_8h_f17_4k/ --dpi 240 -t 23
vid2023_12_8h_4k.bin: frm2023_12_8h.bin plot.py userinfo.db
	python3 plot.py -i frm2023_12_8h.bin -o vid2023_12_8h_4k.bin -of vid2023_12_8h_4k/ --dpi 240 -t 23
vid2024_01_8h_f17_4k.bin: frm2024_01_8h_f17.bin plot.py userinfo.db
	python3 plot.py -i frm2024_01_8h_f17.bin -o vid2024_01_8h_f17_4k.bin -of vid2024_01_8h_f17_4k/ --dpi 240 -t 24


vid2021.mp4: vid2021.bin
	ffmpeg -r 60 -i vid2021/%05d.png -y -loglevel error -stats vid2021.mp4
vid2023_12c.mp4: vid2023_12c.bin
	ffmpeg -r 60 -i vid2023_12c/%05d.png -y -loglevel error -stats vid2023_12c.mp4
vid2023_12.mp4: vid2023_12.bin
	ffmpeg -r 60 -i vid2023_12/%05d.png -y -loglevel error -stats vid2023_12.mp4
vid2023_12q.mp4: vid2023_12q.bin
	ffmpeg -r 60 -i vid2023_12q/%05d.png -y -loglevel error -stats vid2023_12q.mp4
vid2023_12qc.mp4: vid2023_12qc.bin
	ffmpeg -r 60 -i vid2023_12qc/%05d.png -y -loglevel error -stats vid2023_12qc.mp4
vid2023_12q_4k.mp4: vid2023_12q_4k.bin
	ffmpeg -r 60 -i vid2023_12q_4k/%05d.png -y -loglevel error -stats vid2023_12q_4k.mp4
vid2023_12_8h_f17_4k.mp4: vid2023_12_8h_f17_4k.bin
	ffmpeg -r 60 -i vid2023_12_8h_f17_4k/%05d.png -y -loglevel error -stats vid2023_12_8h_f17_4k.mp4
vid2023_12_8h_4k.mp4: vid2023_12_8h_4k.bin
	ffmpeg -r 60 -i vid2023_12_8h_4k/%05d.png -y -loglevel error -stats vid2023_12_8h_4k.mp4
vid2024_01_8h_f17_4k.mp4: vid2024_01_8h_f17_4k.bin
	ffmpeg -r 60 -i vid2024_01_8h_f17_4k/%05d.png -y -loglevel error -stats vid2024_01_8h_f17_4k.mp4


userinfo.db: genuserinfo.py frm2023_12h.bin
	python3 genuserinfo.py -i frm2023_12h.bin -d userinfo.db


.PHONY: clean
clean:
	rm -r tbl* || true
	rm -r frm* || true
	rm -r vid* || true
	rm userinfo.db || true
	rm avatars/ -r || true
