FROM gcc:15.2.0-trixie AS builder

RUN apt install cmake

WORKDIR /app
COPY . .

RUN cmake .
RUN make

FROM debian:trixie

WORKDIR /app

COPY --from=builder /app/koakuma /app/koakuma

CMD ["./koakuma"]
