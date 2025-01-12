all: Dockerfile
	docker images 2>&1 | grep -qe debian/deiso:r2 || docker build -t debian/deiso:r2 .

run-nox:
	docker run -it --rm \
		-h deiso \
		-v /tmp:/tmp/tmp:rw \
		-v /home/oscar/deiso:/root/deiso:rw --name deiso debian/deiso:r2

run:
	xhost +local:root
	docker run -it --rm \
		-h deiso \
		-e DISPLAY=${DISPLAY} \
		--security-opt seccomp=unconfined \
		--cap-add=SYS_PTRACE \
		-v /tmp/.X11-unix:/tmp/.X11-unix:rw \
		-v /tmp:/tmp/tmp:rw \
		-v /home/oscar/deiso/xv6-riscv:/root/deiso:rw --name deiso debian/deiso:r2

run-gdb:
	docker exec -it deiso bash

.PHONY: all run run-nox run-gdb
