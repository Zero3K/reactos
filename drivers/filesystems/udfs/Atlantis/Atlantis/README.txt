Big File Cache

O codigo esta implementado em 5 classes basicas, juntamente com seus teste unitarios
- Config		- Gerencia as configuracoes para execucao deste projeto
- CacheModel	- Implementa o conteiner na especificacao LRU
- Archive		- Implementa o acesso fisico ao arquivo
- MgrCacheFile	- Gerencia o cache de memoria coletando dados do arquivo
- BigFile		- Interface entre o usuario e o cache de memoria

O projeto implementa dois caches:
1 - cache dos registros que foram solicitados e lidos do arquivo em disco
2 - cache dos bolcos de bytes lidos do disco

Procedimento:
Com o objetivo de acessar rapidamente o registro solicitado a ser lido do arquivo, este é "dividido" em blocos e é localizado a a key inicial de cada bloco, que juntamente com o o ponteiro fisico e o tamanho sao armazenado em um vetor.

A solicitar uma registro sua chave é pesquisada no cache de registro(1) e caso seja encontrado é devolvido o valor para o solicitante.

Caso nao esteja no cache, a rotina procura o ponteiro do bloco que contem a chave, via binary search no vetor, e verifica se o bloco esta no cache de blocos(2). Se o bloco nao estiver no cache ele é lido do arquivo e salvo no cache(2).

O proximo passo é localizar o registro da chave no bloco. Isto é feito por pesquisa binaria com a chave desejada. Ao encontrar é e inserido no cache de registro(1) e entregue ao solicitante. Se o registro nao existir um valor vazio é devolvido.