FROM ubuntu:18.04

RUN apt-get update && apt-get install -y gnupg2 iproute2

CMD ["apt-key", "adv", "--keyserver", "hkp://keyserver.ubuntu.com:80", "--recv", "2EE0EA64E40A89B84B2DF73499E82A75642AC823"]