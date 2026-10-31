Cada configuração é uma pasta com TUDO o que é dela: cenas e ajustes (Config), mídia, gráficos, plugins, fotos dos membros, miniaturas, eventos e gravações. **Nada é compartilhado**: mexer num plugin ou numa cena de uma não quebra outra. Da máquina (comuns a todas) ficam os modelos, as credenciais, os logs e o cache.

- `configuraciones_listar`: quais existem, a ativa, sua descrição, tamanho e de onde saiu; e as demos que o programa traz.
- `configuracion_crear {"nombre", "desde", "descripcion"}` cria uma (não a ativa). `desde`: `vacia` (padrão), `actual` (cópia da que está em uso), o nome de outra, `demo:<nombre>` (uma demo do programa) ou `evento:<carpeta>/<inicio|cierre|ultima>` (a configuração com que um evento foi feito: sua foto dos ajustes e a mídia da atual). As gravações não são copiadas, salvo com `con_grabaciones`.
- `configuracion_cambiar {"nombre"}` passa para outra: **o app reinicia** com essa pasta (não copia nada). Corta o que estiver funcionando (transmissão, gravação, conferência): nunca faça isso ao vivo sem que o operador peça.
- `configuracion_editar {"nombre", "renombrar_a"|"descripcion"|"borrar": true}`: renomear, descrever ou mandar para a lixeira (`Configuraciones/.papelera`). A ativa não é renomeada nem apagada.

**A demo**: `demo:Demo parlamentaria` é um sistema deliberativo genérico pronto para testar: o Conselho da Cidade de Puerto Ejemplo (15 membros e presidência com nomes inventados), plenário, câmeras, unidade central de conferência simulada, cenas de assembleia e de debate, superfície de controle e ordem do dia.

**Portátil**: todos os caminhos são salvos relativos à configuração (`Media/logo.png`, também dentro de um pipeline: `location=Media/x.png`). A pasta de dados inteira pode ser copiada para outra máquina do jeito que está. Ao criar uma fonte com um caminho relativo, ele é resolvido em relação à configuração em uso.

Na interface: Ajustes › Sistema › Configurações (usar, duplicar, renomear, apagar, criar a partir de uma demo).