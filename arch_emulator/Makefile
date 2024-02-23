IMAGE_NAME = bbb-cpu-arch
DOCKERFILE_PATH = ./Dockerfile

all: build

run:
	docker run -it -v ./volume:/project bbb-cpu-arch
build:
	docker build -t $(IMAGE_NAME) -f $(DOCKERFILE_PATH) .

clean:
	docker rmi $(IMAGE_NAME)

.PHONY: all build clean run
