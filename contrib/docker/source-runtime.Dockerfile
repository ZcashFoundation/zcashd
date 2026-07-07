FROM debian:trixie-slim

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    bash \
    ca-certificates \
    curl \
    libatomic1 \
    libgomp1 \
  && rm -rf /var/lib/apt/lists/*

ARG ZCASHD_USER=zcashd
ARG ZCASHD_UID=2001

COPY zcashd zcash-cli zcash-tx zcashd-wallet-tool /usr/local/bin/
COPY fetch-params.sh /usr/local/bin/zcash-fetch-params
COPY entrypoint.sh /entrypoint.sh

RUN chmod +x \
      /entrypoint.sh \
      /usr/local/bin/zcashd \
      /usr/local/bin/zcash-cli \
      /usr/local/bin/zcash-tx \
      /usr/local/bin/zcashd-wallet-tool \
      /usr/local/bin/zcash-fetch-params \
  && useradd --home-dir "/srv/$ZCASHD_USER" \
             --shell /bin/bash \
             --create-home \
             --uid "$ZCASHD_UID" \
             "$ZCASHD_USER" \
  && mkdir -p "/srv/$ZCASHD_USER/.zcash/" \
  && touch "/srv/$ZCASHD_USER/.zcash/zcash.conf" \
  && chown -R "$ZCASHD_USER" "/srv/$ZCASHD_USER"

WORKDIR "/srv/$ZCASHD_USER"
ENV HOME="/srv/$ZCASHD_USER"

ENTRYPOINT ["/entrypoint.sh"]
