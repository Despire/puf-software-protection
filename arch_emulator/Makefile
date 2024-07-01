IMAGE_NAME = bbb-cpu-arch
DOCKERFILE_PATH = ./Dockerfile

all: run

run:
	docker build --platform linux/arm32v7 -t $(IMAGE_NAME) -f $(DOCKERFILE_PATH) .
	docker run --platform linux/arm32v7 -it -d -v $(CURDIR)/volume:/project bbb-cpu-arch
clean:
	docker ps --filter "ancestor=$(IMAGE_NAME)" -q | xargs docker stop
	docker ps -a --filter "ancestor=$(IMAGE_NAME)" -q | xargs docker rm
	docker rmi $(IMAGE_NAME)

.PHONY: all run clean
