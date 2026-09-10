#!/bin/sh
set -eu

provas="${TMPDIR:-/tmp}/provar-receita-$$"
trap 'rm -rf "$provas"' EXIT HUP INT TERM
mkdir "$provas"
cat >"$provas/passa" <<'EOF'
#!/bin/sh
exit 0
EOF
cat >"$provas/falha" <<'EOF'
#!/bin/sh
exit 7
EOF
chmod +x "$provas/passa" "$provas/falha"

if sh -c 'for prova do echo "PROVA: $prova"; "$prova" || exit $?; done' sh \
	"$provas/falha" "$provas/passa"; then
	printf '%s\n' 'falha esperada não foi propagada' >&2
	exit 1
fi

sh -c 'for prova do echo "PROVA: $prova"; "$prova" || exit $?; done' sh \
	"$provas/passa"
