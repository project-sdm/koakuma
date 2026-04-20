FROM gcc:15.2.0-trixie AS builder

RUN apt-get update && apt-get install cmake -y

WORKDIR /app
COPY . .

RUN cmake .
RUN make

FROM debian:trixie

WORKDIR /app

COPY --from=builder /app/koakuma /app/koakuma

CMD ["./koakuma"]
