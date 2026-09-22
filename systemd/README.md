# Unidade systemd

Instale binários e unidade:

```sh
install -m 0755 construcao/vramdiskd /usr/local/sbin/vramdiskd
install -m 0755 construcao/vramdiskctl /usr/local/bin/vramdiskctl
install -m 0644 systemd/vramdisk@.service /etc/systemd/system/
install -d -m 0750 /etc/vramdisk
printf 'MEIO=cuda\n' >/etc/vramdisk/0.conf
systemctl daemon-reload
systemctl enable --now vramdisk@0
```

`LimitMEMLOCK=1G` é exemplo seguro. Aumente-o somente após calcular
`filas × profundidade × maior operação` e a reserva pedida. Configuração
inválida falha antes da publicação do bloco.

Diagnóstico e termo:

```sh
systemctl status vramdisk@0
journalctl -u vramdisk@0
systemctl stop vramdisk@0
```

`WatchdogSec=0` declara que o MVP ainda não envia `sd_notify`; o prazo real
pertence às operações e à parada limitada. A unidade não altera `sysctl`.
