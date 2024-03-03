IMAGE_NAME = bbb-cpu-arch
DOCKERFILE_PATH = ./Dockerfile

all: run

run:
	docker build -t $(IMAGE_NAME) -f $(DOCKERFILE_PATH) .
	docker run -it -d -v ./volume:/project bbb-cpu-arch
clean:
	docker ps --filter "ancestor=$(IMAGE_NAME)" -q | xargs docker stop
	docker ps -a --filter "ancestor=$(IMAGE_NAME)" -q | xargs docker rm
	docker rmi $(IMAGE_NAME)

.PHONY: all run clean
