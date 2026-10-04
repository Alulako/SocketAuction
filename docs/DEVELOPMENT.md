# Manutenção e validação

Este documento registra os cuidados necessários ao modificar o SocketAuction.

## Antes de alterar o código

- Consulte [a arquitetura](ARCHITECTURE.md) e [o protocolo](PROTOCOL.md) para preservar a compatibilidade.
- Evite alterações simultâneas em mecanismos distintos sem testes intermediários.
- Preserve as regiões críticas que protegem lances, histórico, registro de usuários e envios.

## Compilação e testes

O projeto é compilado com GCC e os avisos `-Wall -Wextra -Wpedantic`.

```bash
make clean
make
make test-all
```

A suíte automatizada verifica concorrência, eventos em tempo real, histórico, recuperação após erros, encerramento das conexões e enquadramento de mensagens TCP. Os testes de borda usam a biblioteca padrão do Python 3; a aplicação em si não depende de Python.

## Revisão de alterações

Antes de registrar alterações no repositório, confirme que:

1. O projeto compila sem novos avisos.
2. Os testes relevantes passam.
3. O protocolo e o README correspondem ao comportamento implementado.
4. Os comentários explicam decisões técnicas, sem repetir operações óbvias.
5. Não foram adicionados executáveis compilados nem arquivos temporários ao Git.

O uso de ferramentas de apoio no desenvolvimento, quando sujeito a regras específicas da disciplina, deve ser declarado conforme essas regras.
