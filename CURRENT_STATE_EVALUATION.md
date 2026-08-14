# Avaliação do Estado Atual e Direção Futura do Projeto Liquid

> Snapshot histórico: esta avaliação antecede a conclusão de M6 e o plano aprovado S0-S7 para Solid v0.1. Consulte `COMPLETE_SOLID.md` e `DEVELOPMENT_TRACKING.md` para o estado atual.

**Data da avaliação:** 11 de julho de 2026; atualização de M5 em 10 de agosto de 2026

**Referência do código:** working tree atual, incluindo a estabilização pré-M5 de 15 de julho de 2026

**Escopo implementado:** Stage 1, Solid, com M1 a M5 concluídos

**Marco corrente declarado:** M6, Simulation CLI

**Natureza deste documento:** avaliação técnica e científica; não é parecer jurídico, médico ou regulatório

> **Atualização de 10 de agosto de 2026:** o baseline pré-M5 foi reconciliado abaixo com o estado preparado para publicação. A fronteira M5 possui Lua 5.4.8 fixado, runner restrito, capabilities tipadas, propostas transacionais, limites de recursos e regressões. O proprietário declarou M5 concluído e promoveu M6, Simulation CLI, para `Current`.

## Sumário

1. [Resumo executivo](#1-resumo-executivo)
2. [Escopo e método da avaliação](#2-escopo-e-método-da-avaliação)
3. [Estado real do projeto](#3-estado-real-do-projeto)
4. [Conformidade com AGENTS.md](#4-conformidade-com-agentsmd)
5. [Achados técnicos priorizados](#5-achados-técnicos-priorizados)
6. [Consistência da documentação](#6-consistência-da-documentação)
7. [Terminologia para sinais fisiológicos e comportamento](#7-terminologia-para-sinais-fisiológicos-e-comportamento)
8. [O que os sinais podem e não podem sustentar](#8-o-que-os-sinais-podem-e-não-podem-sustentar)
9. [Arquitetura futura para biossinais](#9-arquitetura-futura-para-biossinais)
10. [Privacidade, ética e fronteira regulatória](#10-privacidade-ética-e-fronteira-regulatória)
11. [Contrato recomendado para M5](#11-contrato-recomendado-para-m5)
12. [Roteiro recomendado](#12-roteiro-recomendado)
13. [Registro de riscos](#13-registro-de-riscos)
14. [Decisões consolidadas](#14-decisões-consolidadas-desta-avaliação)
15. [Avaliação final](#15-avaliação-final-do-trabalho-realizado)
16. [Fontes externas](#16-fontes-externas-selecionadas)

---

## 1. Resumo executivo

O trabalho realizado até aqui é uma base de pesquisa séria e bem delimitada. O maior acerto do projeto não é apenas técnico: é a disciplina de separar o núcleo determinístico Solid das futuras camadas adaptativas e da aplicação Liquid Layer. Essa divisão reduz o risco de misturar inferência incerta, estado físico e efeitos externos antes de existirem regras claras de propriedade, permissão, arbitragem e rastreabilidade.

O repositório comprova de forma convincente o caminho feliz de M1 a M4:

- identidades recicláveis de comportamentos e intents;
- componentes tipados e nomeados, com acesso compartilhado por comportamento;
- intents imutáveis pela API pública normal;
- limpeza de intents quando o proprietário ou alvo deixa de ser válido;
- resolução determinística por prioridade e maior `IntentId`;
- associação de comportamentos a sistemas por assinatura;
- execução de sistemas em ordem de registro;
- frame loop com tempo explícito não decrescente, sistemas antes da resolução e log de sucesso ou falha.

As compilações Debug e Release passam com 12/12 testes efetivamente ativos. A compilação com AddressSanitizer e UndefinedBehaviorSanitizer também passa quando a detecção de vazamentos é desabilitada, porque o LeakSanitizer não funciona sob o ambiente `ptrace` usado nesta avaliação.

O núcleo ainda é um protótipo pré-alfa, não um runtime pronto para produção ou estudo com participantes. A estabilização pré-M5 de 15 de julho fechou o pacote estrutural identificado nesta revisão:

1. copy/move inseguro de `World`, `Runtime` e managers stateful foi eliminado dessas APIs;
2. callbacks de membership concluem transições estruturalmente consistentes antes de propagar erro, e reentrância de topologia é rejeitada;
3. registro de sistemas recebe assinatura inicial atomicamente, e membership público manual foi removido;
4. storages e operações multi-índice possuem liveness explícita, destruição imediata de valores e rollback testado;
5. lifetime, prioridade, tempo, ponteiros, thread confinement e tie-break foram validados ou documentados;
6. o harness mantém asserts ativos em Release e oferece warnings estritos, warnings-as-errors e sanitizers opcionais.

M5 está concluído e sua fronteira técnica está implementada: Lua 5.4.8 fixado no CMake, diretório `scripting/`, runner restrito, codecs tipados, capabilities por acesso, propostas transacionais, limites de recursos e `test_lua_behavior.cpp`. Os critérios técnicos estão cobertos e M6, Simulation CLI, passa a ser o marco corrente.

Sobre sinais fisiológicos, a direção cientificamente defensável não é construir um “leitor de emoções” ou um “detector de comportamento”. A formulação recomendada é:

> **Liquid Layer é um ambiente de apoio psicofisiológico centrado na pessoa e sensível ao contexto, capaz de usar mudanças incertas em relação à linha de base individual para oferecer adaptações limitadas, reversíveis e controladas pela pessoa.**

ECG/VFC, PPG, EDA, EEG, respiração e temperatura cutânea podem fornecer correlatos de ativação, carga, fadiga ou contexto. Nenhum desses sinais identifica sozinho uma emoção, intenção, diagnóstico ou necessidade de apoio. A literatura favorece modelos individualizados, fusão multimodal, controle de qualidade do sinal, contexto ambiental e avaliação ecológica momentânea, sempre com possibilidade de abstenção e confirmação humana.

**Avaliação geral:** o projeto está bem encaminhado para o estágio em que se encontra. A separação `Runtime -> World`, os invariantes pré-M5 e a fronteira Lua agora possuem contratos executáveis, não apenas intenção documental. O próximo passo de produto continua sendo uma decisão explícita do proprietário; coleta de dados fisiológicos permanece fora do escopo atual.

---

## 2. Escopo e método da avaliação

### 2.1 Cobertura local

Foram revisados os 42 arquivos do estado preparado para publicação:

| Categoria | Quantidade |
|---|---:|
| Headers em `include/` | 13 |
| Implementações em `src/` | 9 |
| Testes em `tests/` | 12 |
| Documentos Markdown | 5 |
| Build e configuração | 3 |

Também foram examinados:

- histórico Git de M1 a M4;
- estado atual da árvore de trabalho;
- configuração CMake, sanitizers e warnings;
- coerência entre `AGENTS.md`, `DEVELOPMENT_TRACKING.md`, arquitetura e notas do artigo;
- padrões relevantes do repositório local Superposition;
- contratos de propriedade, permissão, expiração, resolução, callbacks, cópia, reentrância e reciclagem;
- lacunas de teste negativas para M5;
- literatura e orientações oficiais sobre psicofisiologia, wearables, neurodivergência, LGPD, ética em pesquisa e a fronteira de Software as a Medical Device.

### 2.2 Verificações executadas

| Verificação | Resultado |
|---|---|
| Configuração e build Debug com GCC 14.2 | Passou |
| CTest Debug | 12/12 passaram |
| Build ASan + UBSan | Passou |
| CTest ASan + UBSan com `ASAN_OPTIONS=detect_leaks=0` | 12/12 passaram |
| LeakSanitizer | Não verificável sob `ptrace` neste ambiente |
| libstdc++ debug iterators/assertions | 12/12 passaram no estado M5 preparado para publicação |
| Compilação isolada de cada header público | Passou |
| Build Release | Compilou |
| CTest Release com asserts ativos nos alvos de teste | 12/12 passaram |
| Build com `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror` | Passou; 12/12 testes passaram |
| Build com `BUILD_SHARED_LIBS=ON` | Passou; 12/12 testes passaram |
| Configuração offline com fonte Lua previamente verificada | Passou |
| CI, cobertura e análise estática dedicada | Não existem no repositório |

### 2.3 Limites desta avaliação

- A integração de modelo/LLM e o manifesto machine-readable de schemas ainda não existem; M5 testa somente a fronteira Lua controlada.
- Não existem adapters, efeitos físicos, eventos, rede ou persistência.
- Não há dados fisiológicos, participantes, modelos de ML ou hardware no repositório.
- LeakSanitizer não pôde ser executado de forma válida no sandbox.
- A atualização de M5 adicionou código de produção, CMake, testes e documentação dentro do escopo permitido.

---

## 3. Estado real do projeto

### 3.1 Maturidade por camada

| Área | Estado | Avaliação |
|---|---|---|
| Disciplina de escopo | Forte | Os limites negativos de M5 são claros e foram respeitados. |
| Arquitetura do núcleo | Forte para protótipo | Propriedade e responsabilidades estão bem separadas. |
| Comportamento M1-M4 | Funcional | Caminhos normais e vários casos de reciclagem/limpeza estão cobertos. |
| Robustez de ciclo de vida | Estabilizada para M5 | Copy/move, frame fail-stop, reentrância, callbacks, slots e operações multi-índice possuem contratos e regressões. |
| Estratégia de testes | Boa para o estágio | Debug, Release, warnings-as-errors, iteradores de debug e ASan/UBSan passam; ainda usa `assert` em vez de framework estruturado. |
| Documentação operacional | Boa | `AGENTS.md` é claro e atual. |
| Documentação consolidada | Alinhada para M5 | Fronteira M4 e contrato de sandbox M5 estão consolidados; decisões futuras permanecem explicitamente abertas. |
| M5 Lua | Não iniciado | Existe intenção e critério, mas nenhuma implementação. |
| Liquid adaptativo | Conceitual | Correto para a etapa atual. |
| Liquid Layer e biossinais | Conceitual | Ainda não está pronto para protocolo ou coleta humana. |

### 3.2 Mapa arquitetural atual

```text
Runtime
  owns World
    owns WorldState
      BehaviorRegistry
      ComponentRegistry
      IntentRegistry
      SystemRegistry
      behavior signatures
    uses Coordinator as internal consistency layer

Frame:
  begin_frame
  -> expire_intents
  -> run systems in registration order
  -> resolve explicit intent requests
  -> end_frame
```

O fluxo de uma mudança pretendida é:

```text
trusted C++ caller/system
  -> World validates behavior and write access
  -> IntentRegistry stores immutable typed record
  -> expiration/cleanup removes invalid records
  -> resolution chooses an IntentId
  -> future stage applies an effect
```

Essa arquitetura é adequada à tese do projeto: inferência e scripts podem propor; o núcleo determinístico valida, limita, arbitra e registra.

### 3.3 Estado por marco

| Marco | Evidência atual | Veredito |
|---|---|---|
| M1, ECS modificado | IDs, componentes, acessos, assinaturas, sistemas e testes de estresse | Concluído, com riscos de ciclo de vida identificados |
| M2, lifetime/expiração | `Persistent`, `UntilTime`, limpeza explícita | Concluído |
| M3, resolução | prioridade, maior ID, índices por alvo, limpeza na resolução | Concluído |
| M4, frame mínimo | `World`, `WorldState`, runtime, ordem de sistemas e frame log | Concluído |
| M5, Lua | Lua 5.4.8, runner restrito, codecs, capabilities, transação, limites e regressões | Concluído |
| M6, simulation CLI | Escopo determinístico e sem hardware definido no tracking | Corrente; implementação ainda não iniciada |

### 3.4 Comparação com o Superposition

O uso do Superposition como referência local é visível e, em geral, bem adaptado:

- Liquid preserva storage tipado por template, assinaturas em bitset, membership coordenado e callbacks de entrada/saída;
- melhora a referência com nomes estáveis explícitos, handles `ComponentType<T>`, checagem de tipo em runtime, componentes nomeados compartilhados, modos de acesso, ordem determinística de sistemas e limpeza coordenada de intents;
- troca os arrays densos com swap-remove do Superposition por slots estáveis recicláveis, apropriados a nomes e handles, ao custo de tombstones e buscas lineares;
- amplia o impacto de callbacks: no Superposition, falha de callback afeta membership; em Liquid, ela pode interromper invariantes que atravessam componentes, assinaturas, intents e behaviors;
- divergiu inicialmente no ownership. O `Coordinator` do Superposition possui managers por `unique_ptr` e é naturalmente não copiável. Em Liquid, `WorldState` por valor mais `Coordinator` com referência exigiu apagar copy/move explicitamente; esse contrato agora é verificado em compilação.

Conclusão: a direção de design é consistente com a referência, mas a adaptação precisa recuperar sua garantia de ownership exclusivo.

---

## 4. Conformidade com `AGENTS.md`

| Regra operacional | Estado | Evidência e observação |
|---|---|---|
| Trabalhar apenas no Stage 1 | Conforme | Não há LLM, adapters, MQTT, voz, CLI ou app final. |
| `World` como fronteira pública | Conforme com ressalva | O fluxo normal passa por `World`; alguns métodos públicos retornam acesso mutável confiável. |
| `Coordinator` como consistência interna | Conforme | O `World` encapsula `WorldState` e encaminha ao coordenador. |
| Comportamentos como identidade | Conforme | `BehaviorRegistry` é a origem de IDs e pools de intents. |
| Componentes compartilhados e nomeados | Conforme | Nome e slot ficam no `ComponentRegistry`; acessos ficam no storage tipado. |
| Intents imutáveis após criação | Conforme pela API normal | Consultas retornam referências `const`; não existe setter semântico. |
| Dono e alvo indexados | Conforme | `byOwner` e `byTarget` são mantidos no `IntentRegistry`. |
| Criação via World exige escrita | Conforme | `Coordinator::create_intent` valida dono, slot e permissão. |
| Remoção de alvo/permissão limpa intents | Conforme | Implementado no coordenador e coberto por testes. |
| Criação/destruição de comportamento alinha pool | Conforme | `create_behavior_pool` e `destroy_owned_by` acompanham o ciclo. |
| Resolução determinística | Conforme | Maior prioridade, depois maior `IntentId`. |
| Ordem de sistemas por registro | Conforme | `registrationOrder` é explícita. |
| Tempo sem relógio global | Conforme | `IntentTime now` é injetado em milissegundos monotônicos relativos à sessão. |
| Não armazenar ponteiros de componente a longo prazo | Advertência documental | A API retorna ponteiros que podem ser invalidados; a regra depende de disciplina do chamador. |
| Lua só cria novas intents por API controlada | Conforme | `propose(request)` valida e bufferiza; commit posterior cria somente intents novas. |
| Lua não acessa registros, storage ou mundo físico | Conforme | `_ENV` restrito expõe snapshots copiados e closures host-bound; `World` e managers não são userdata. |
| Teste `test_lua_behavior.cpp` | Presente | Cobre fluxo válido, sandbox, permissões, cache, budgets, rollback e integração com `Runtime`. |

### Conclusão de conformidade

O código M1-M5 é coerente com as regras operacionais atuais. A fronteira Lua satisfaz os critérios técnicos exercitados pela suíte e M5 foi declarado concluído pelo proprietário.

---

## 5. Achados técnicos priorizados

### 5.1 Alta prioridade

#### H1. Resolvido: cópia e movimento de `World`/`Runtime` ligavam o coordenador ao estado errado

**Referências:** `include/liquid/world/World.hpp`, `include/liquid/detail/Coordinator.hpp`, `include/liquid/Runtime.hpp`.

`World` possui um `WorldState` por valor e um `Coordinator` que guarda `WorldState&`. A versão avaliada originalmente permitia operações implícitas que copiavam essa referência. Agora `World`, `Runtime`, `Coordinator`, `ComponentRegistry`, `IntentRegistry` e `SystemRegistry` apagam copy e move; testes `static_assert` congelam esse contrato.

O risco histórico também existia abaixo de `World`, porque índices eram copiados enquanto storages/sistemas permaneciam compartilhados por `shared_ptr`. Não existe mais cópia pública implícita desses managers.

**Impacto anterior:** aliasing entre mundos, mutação do objeto errado e potencial use-after-free.

**Decisão aplicada:** clonagem ou movimento só volta quando houver semântica explícita e testes de rebinding/deep copy.

#### H2. Resolvido: callbacks preservam consistência estrutural

**Referências:** `src/world/Coordinator.cpp:17-25`, `src/SystemRegistry.cpp:38-43`, `src/SystemRegistry.cpp:47-58`, `src/Runtime.cpp:17-47`.

O registry conclui todas as transições de membership e relança a primeira exceção de callback somente depois de restaurar consistência. Criação de comportamento e registro de sistema revertem o objeto recém-criado quando a notificação falha; mudanças estruturais já iniciadas permanecem aplicadas e consistentes. Callbacks são observacionais por contrato, não devem lançar nem tentar alterar topologia, e o `World` bloqueia reentrância estrutural também durante essas notificações.

O contrato de frame continua fail-stop para exceções que escapem de `System::run`. O contrato M5 agora exige que erros Lua sejam resultados explícitos e revertam somente as intents criadas pela execução, sem escapar para o runtime.

#### H3. Resolvido: mutação reentrante do `SystemRegistry` podia produzir use-after-free

**Referências:** `include/liquid/world/World.hpp`, `include/liquid/detail/SystemRegistry.hpp`, `src/SystemRegistry.cpp`.

Na versão inicialmente avaliada, `run_systems` copiava apenas os `type_index` da ordem e chamava `run()` pelo único `shared_ptr` do mapa. Um teste direcionado com ASan confirmou `heap-use-after-free` após autodestruição.

Agora o registry mantém uma referência forte local durante execução e rejeita mudanças estruturais durante `run()` e callbacks. `World` também congela comportamento, componente, permissão e topologia de sistemas durante dispatch, protegendo os slots pré-computados usados pela resolução do mesmo frame.

**Impacto anterior:** corrupção de memória e comportamento indefinido.

**Decisão aplicada:** rejeição imediata com `std::logic_error`, sem fila implícita para o próximo frame.

#### H4. Resolvido para o harness atual: a suíte baseada em `assert` não era válida em Release

**Referências:** `CMakeLists.txt:45-65`, `tests/test_intent_registry.cpp:152-164`, `tests/test_stress.cpp:52`, `tests/test_stress.cpp:101`, `tests/test_intent_expiration.cpp:57`.

Os testes usam `assert` como framework. Originalmente, `-DNDEBUG` removia verificações e efeitos colaterais; o CTest Release obtinha 10/11. O CMake agora remove `NDEBUG` somente dos executáveis de teste, preservando todas as verificações em configurações otimizadas. Debug, Release e ASan/UBSan passam 12/12.

**Impacto anterior:** Release podia aprovar testes vazios, falhar por uma dinâmica diferente de Debug ou deixar de executar operações essenciais.

**Decisão aplicada:** compilar alvos de teste explicitamente sem `NDEBUG`. Migrar efeitos colaterais para fora de `assert` e adotar um framework continuam melhorias futuras, não bloqueadores do contrato atual.

#### H5. Resolvido: contrato de segurança e execução de M5 implementado

**Referências:** `AGENTS.md:29-55`, `AGENTS.md:224-233`, `DEVELOPMENT_TRACKING.md:404-421`, `CMakeLists.txt:9-68`.

M5 implementa as decisões obrigatórias da fronteira:

- versão e forma de integração com Lua;
- bibliotecas Lua permitidas;
- limite de instruções e memória;
- política de tempo e aleatoriedade determinísticos;
- ligação imutável entre execução e `BehaviorId`;
- registro de tipos de componente expostos;
- política atômica em caso de erro parcial;
- isolamento de erro para não abortar o frame;
- formato mínimo de diagnóstico e auditoria.

**Decisão aplicada:** M5 usa um executor de fronteira estreita, invocado explicitamente. O host escolhe o proprietário e registra capacidades tipadas; Lua não recebe `World`, registry, coordinator, slot cru ou argumento de proprietário. A execução possui limites de instrução, memória Lua, valores host bufferizados, strings, tabelas, fonte, diagnóstico e intents criadas, com commit posterior, rollback local e erros contidos.

Para eliminar duas decisões pendentes, esta avaliação adota como padrão de M5:

- **integração:** API C oficial do Lua 5.4, fixada inicialmente em 5.4.8; não adicionar uma biblioteca de binding até um spike curto demonstrar redução real de complexidade sem ampliar a superfície exposta;
- **tempo:** `IntentTime` representa milissegundos inteiros, monotônicos e relativos ao início da sessão do runtime, nunca Unix time. Chamadas de frame devem ser não decrescentes. Lua recebe uma duração limitada, e o host calcula `expiresAt` com verificação de overflow.

Lua 5.5 é a versão mais nova da linguagem, mas M5 não depende de seus recursos. A série 5.4 possui ABI estável dentro da série, manual consolidado e é suficiente para a fronteira pequena proposta. A escolha deve ser registrada e reproduzida pelo CMake, não resolvida implicitamente pelo sistema operacional ([histórico oficial](https://www.lua.org/versions.html)).

### 5.2 Prioridade média

#### M1. Resolvido: sistemas de assinatura vazia recebem comportamentos existentes

**Referências:** `src/world/Coordinator.cpp`, `include/liquid/detail/Coordinator.hpp`, `src/SystemRegistry.cpp`.

Criação de comportamento registra a assinatura vazia e atualiza membership. Registro de sistema recebe a assinatura inicial atomicamente, percorre os comportamentos vivos e cobre as ordens behavior-antes-system e system-antes-behavior. `Coordinator` é o único proprietário da derivação de membership; o `World` não expõe overrides manuais.

#### M2. A fronteira C++ pública contém caminhos mutáveis sem permissão de comportamento

**Referências:** `include/liquid/world/World.hpp`, `include/liquid/detail/Coordinator.hpp`.

`get_component_named` e `resolve_component` podem retornar ponteiro mutável sem `BehaviorId`. Isso é aceitável para código C++ confiável de aplicação/efeito, mas contradiz a ideia de que resolução por slot é apenas interna e seria perigoso em bindings.

**Impacto:** a segurança de M5 depende completamente de não expor a classe `World` ou userdata equivalente.

#### M3. Resolvido por contrato: ponteiros de componente são empréstimos curtos

**Referências:** `include/liquid/detail/ComponentStorage.hpp`.

Os ponteiros apontam para elementos de `std::vector`. Adicionar outro componente do mesmo tipo pode realocar o vetor. Remoção e reuso do slot podem fazer um handle antigo designar um componente novo.

**Impacto:** sistemas ou adapters que guardem ponteiros entre frames podem ler ou escrever o objeto errado.

**Decisão aplicada:** validade somente até a próxima mutação estrutural daquele storage tipado. Handles são o estado duradouro; Lua nunca recebe esses ponteiros como userdata.

#### M4. Resolvido por contrato: thread confinement declarado

Os registries usam mapas, vetores, sets, referências e ponteiros sem sincronização. Isso é adequado a um loop determinístico single-thread, mas nada na API impede uma futura thread de sensor, Lua ou LLM de acessar `World` diretamente.

**Decisão aplicada:** `World` e `Runtime` pertencem a uma única thread. Trabalho assíncrono futuro devolve propostas por fila e só a thread do frame cria intents.

#### M5. Decisão fixada por teste: maior `IntentId` não significa intent mais recente após reciclagem

**Referências:** `DEVELOPMENT_TRACKING.md:341-345`, `src/IntentRegistry.cpp:159-168`.

O contrato atual é explicitamente “maior ID”, e a implementação o cumpre. Após reciclagem, um intent recém-criado com ID 1 perde para um intent mais antigo com ID 2. Isso não é um bug do contrato atual, mas scripts podem interpretar equivocadamente como “last write wins”.

**Decisão aplicada:** o teste de reciclagem fixa a regra de maior `IntentId`. Se recência se tornar requisito, uma sequência de criação separada pertence a um marco posterior.

#### M6. Resolvido: expiração por frame sem varredura repetida por alvo e tempo documentado

**Referências:** `src/Runtime.cpp:27-35`, `src/IntentRegistry.cpp:139-150`, `include/liquid/IntentLifetime.hpp:7-31`.

O runtime expira antes dos sistemas e uma segunda vez antes da seleção para remover intents criadas no mesmo frame que já nasceram expiradas. A seleção por componente não repete a varredura global. Resolução direta pelo manager preserva seu contrato autocontido.

`IntentTime` é documentado como milissegundos monotônicos desde o início da sessão. `now >= expiresAt` permanece a fronteira; Lua aceita `persistent` ou duração com overflow verificado, nunca epoch ou tempo absoluto escolhido pelo script.

#### M7. Resolvido: liveness de slot em tempo constante

**Referências:** `include/liquid/detail/ComponentStorage.hpp`.

Slots usam `std::optional` para representar liveness. `has`, `remove` e acesso verificam presença em tempo constante, e remoção destrói imediatamente o recurso armazenado.

#### M8. Resolvido: warnings estritos disponíveis e limpos

**Referências:** `include/liquid/detail/ComponentStorage.hpp`, `tests/test_intent_resolution.cpp`.

O CMake oferece warnings estritos e modo opcional warnings-as-errors. A base compila limpa com `-Wall -Wextra -Wpedantic -Wconversion -Werror` no toolchain atual.

#### M9. Resolvido: lifetime e prioridade inválidos são rejeitados

**Referências:** `include/liquid/IntentLifetime.hpp:14-31`, `include/liquid/Ids.hpp:60-64`, `src/IntentExpiration.cpp:11-20`, `src/IntentRegistry.cpp:7-9`.

`IntentRegistry` valida kind, coerência de expiração e prioridades enumeradas; a avaliação de expiração também rejeita kind desconhecido. Lua e futura desserialização devem conservar a mesma fronteira fechada.

#### M10. Resolvido: criação multi-índice possui rollback

**Referências:** `include/liquid/detail/IntentRegistry.hpp`, `include/liquid/detail/ComponentRegistry.hpp`.

Criação de intent, registro de tipo e adição de componente usam etapas com rollback e testes com valores que lançam durante construção/movimento. Isso protege os índices locais; o `World` inteiro continua sem transação geral. O runner Lua não cria intents enquanto o script roda e, em falha durante commit, reverte somente os IDs já criados por aquela execução.

### 5.3 Baixa prioridade e dívida operacional

- Não há README, licença, CI, cobertura, ADRs ou tabela de rastreabilidade marco-teste-commit.
- Classes principais estão no namespace global enquanto vários tipos vivem em `liquid`, criando uma API visualmente irregular.
- Algumas consultas usam `catch (...)` para retornar `false`, o que também mascara falhas inesperadas como alocação.
- Há duplicação simples entre overloads de expiração para registry, coordinator e world.
- O estilo preexistente diverge em pontos menores das preferências atuais; isso não justifica reformatar arquivos fora de mudanças funcionais.

---

## 6. Consistência da documentação

### 6.1 Fonte de autoridade após o alinhamento

A leitura recomendada continua sendo:

1. `AGENTS.md`: autoridade operacional e de escopo;
2. `DEVELOPMENT_TRACKING.md`: autoridade de status e sequência de marcos;
3. `Liquid_Concepts_and_Architecture.md`: arquitetura implementada mais visão futura, sujeita a atualização;
4. `ARTICLE_NOTES.md`: notas históricas não normativas.

### 6.2 Divergências concretas restantes

| Tema | Divergência |
|---|---|
| Linguagem de scripting | M5 já decidiu Lua; ainda falta transformar a decisão em contrato de host e implementação. |
| Árvore “corrente” | A árvore ampla da arquitetura é uma direção futura legítima, mas o rótulo “Current intended” é ambíguo; em paralelo, o tracking mostra uma árvore mínima congelada em M1 em `DEVELOPMENT_TRACKING.md:90-133`. |
| Identidade de Agent | A arquitetura coloca Agent “ao lado” de Behavior em `:142-144`; `AGENTS.md:141-144` determina que Agent será um Behavior com componente `Agent`. |
| Estado dos marcos | Partes de tracking e arquitetura ainda falam de resolução/runtime como futuro, apesar de M3/M4 concluídos. |

### 6.3 Avaliação documental

A documentação agora converge sobre a fronteira `Runtime -> World`, a ordem do frame e o papel interno de `Coordinator`. Antes de publicar artigo ou abrir colaboração externa, ainda vale consolidar:

- estado implementado versus visão futura;
- semântica de Agent;
- decisão Lua;
- clock de `IntentTime`;
- estratégia de testes;
- riscos e garantias de scripting.

Vocabulário, hipóteses, protocolo e governança de biossinais também precisarão entrar na documentação antes do Stage 3. Sua ausência agora é deliberada e coerente com o escopo, não uma não conformidade de M5.

---

## 7. Terminologia para sinais fisiológicos e comportamento

### 7.1 Termo guarda-chuva recomendado

Para o projeto, a formulação mais precisa é:

**monitoramento psicofisiológico multimodal, personalizado e sensível ao contexto para apoio ambiental adaptativo**

Ela é preferível a “monitoramento do comportamento por ECG/EEG”, porque separa:

- o que é medido: sinais fisiológicos e contexto;
- o que é estimado: estado incerto ou mudança relativa;
- o que é observado diretamente: comportamento, autorrelato ou tarefa;
- o que o sistema faz: oferece apoio ambiental reversível.

“Computação fisiológica” é o campo de sistemas que usam psicofisiologia em tempo real como entrada para adaptação. “Psicofisiologia” estuda relações entre processos psicológicos e fisiológicos. Nenhum dos termos autoriza inferir causalidade ou diagnóstico. A definição de computação fisiológica é compatível com o trabalho clássico de Fairclough, que também ressalta os desafios conceituais e metodológicos do campo ([DOI](https://doi.org/10.1016/j.intcom.2008.10.011)).

### 7.2 Vocabulário recomendado

| Português | Inglês | Uso recomendado |
|---|---|---|
| sinais fisiológicos / biossinais | physiological signals / biosignals | Termo geral de aquisição |
| psicofisiologia | psychophysiology | Relação entre processos psicológicos e fisiologia |
| monitoramento psicofisiológico | psychophysiological monitoring | Melhor guarda-chuva para o projeto |
| computação fisiológica | physiological computing | Sistema que adapta usando fisiologia em tempo real |
| computação afetiva | affective computing | Campo mais amplo; usar com cautela |
| estimativa de estado afetivo | affect-state estimation | Preferível a “detecção de emoção” |
| ativação autonômica/fisiológica | autonomic/physiological activation | Preferível a rotular automaticamente como estresse |
| atividade eletrodérmica | electrodermal activity, EDA | Termo guarda-chuva preferido |
| condutância da pele | skin conductance | Uma medida EDA; SCL tônico e SCR fásico |
| resposta galvânica da pele | galvanic skin response, GSR | Sinônimo histórico menos preciso |
| variabilidade da frequência cardíaca | heart-rate variability, HRV | Variação entre intervalos cardíacos |
| fotopletismografia | photoplethysmography, PPG | Medida óptica de pulso |
| variabilidade da frequência de pulso | pulse-rate variability, PRV | Derivada de PPG; não igualar silenciosamente a HRV de ECG |
| temperatura cutânea/periférica | skin/peripheral temperature | Não é temperatura corporal central |
| carga cognitiva | cognitive load | Constructo de teoria cognitiva |
| carga mental de trabalho | mental workload | Demanda da tarefa/operador; relacionada, não idêntica |
| avaliação ecológica momentânea | ecological momentary assessment, EMA | Autorrelato contextual em tempo real |
| avaliação fisiológica ecológica | ecological physiological assessment, EPA | Fisiologia ambulatorial no cotidiano |
| fusão multimodal | multimodal fusion | Integração de modalidades complementares |
| computação sensível ao contexto | context-aware computing | Usa atividade, ambiente, tempo e situação |
| inteligência ambiental | ambient intelligence | Ambiente instrumentado e responsivo |
| fenotipagem digital | digital phenotyping | Usar apenas se houver hipótese de saúde e governança compatível |
| biomarcador digital | digital biomarker | Reservar para medida validada e finalidade bem definida |
| intervenção adaptativa just-in-time | just-in-time adaptive intervention, JITAI | Intervenção decidida por estado/contexto recente |

### 7.3 Termos e alegações a evitar

Evitar sem validação específica:

- “lê emoções”;
- “detecta estresse”;
- “mede comportamento”;
- “prevê meltdown ou agressividade”;
- “identifica autismo/TDAH”;
- “sabe o que a pessoa precisa”.

Preferir:

- “estima evidência compatível com aumento de ativação”;
- “detecta mudança relativa à linha de base individual”;
- “combina sinal, qualidade, contexto e autorrelato”;
- “oferece uma adaptação configurada pela pessoa”;
- “abstém-se quando a qualidade ou confiança é insuficiente”.

### 7.4 Consultas úteis para pesquisa bibliográfica

Em português:

```text
"monitoramento psicofisiológico" comportamento cotidiano
"sinais fisiológicos" "ambientes inteligentes" neurodivergência
"atividade eletrodérmica" ativação autonômica contexto
"variabilidade da frequência cardíaca" stress wearables
"fusão multimodal" biossinais comportamento
"avaliação ecológica momentânea" fisiologia
"computação fisiológica" sistemas adaptativos
"intervenção adaptativa just-in-time" saúde comportamento
```

Em inglês:

```text
ambulatory psychophysiological monitoring behavior
wearable physiological sensing context-aware adaptation
physiological computing adaptive environment
multimodal biosignal fusion affective state estimation
ecological momentary assessment physiological activation
personalized stress arousal estimation wearable
neurodiversity participatory design wearable sensing
just-in-time adaptive intervention psychophysiology
```

Consulta booleana inicial:

```text
(ECG OR HRV OR PPG OR EDA OR EEG OR "skin temperature")
AND (behavior OR stress OR affect OR workload OR self-regulation)
AND (wearable OR ambulatory OR "smart environment")
AND (personalized OR multimodal OR contextual)
```

---

## 8. O que os sinais podem e não podem sustentar

### 8.1 Princípio central

ECG/VFC, EDA, EEG, respiração e temperatura cutânea medem processos fisiológicos. Eles podem se correlacionar com ativação, esforço, atenção, fadiga, dor, afeto ou estresse em condições definidas. Não há relação um-para-um confiável entre um sinal periférico e uma emoção discreta, intenção ou necessidade de apoio. Revisões de especificidade autonômica e carga mental apontam heterogeneidade e dependência de tarefa/contexto ([autonomic-emotion review](https://pubmed.ncbi.nlm.nih.gov/24388802/), [mental-workload review](https://pubmed.ncbi.nlm.nih.gov/30487103/)).

### 8.2 Mapa por modalidade

| Modalidade | O que mede | Valor potencial | Limitações principais | Prioridade sugerida |
|---|---|---|---|---|
| ECG | atividade elétrica cardíaca | R-peaks, frequência e VFC com boa referência temporal | eletrodos, movimento, respiração, postura, medicação, duração | Piloto controlado ou referência |
| PPG | pulso óptico periférico | uso diário menos invasivo, HR e PRV | movimento, perfusão, posição e desempenho dependente do dispositivo/pigmentação; PRV não é automaticamente HRV | Primeiro wearable, com IMU |
| EDA | atividade sudomotora simpática | mudança de ativação fisiológica | não informa causa/valência; calor, movimento, fala e pressão interferem | Primeiro stack multimodal |
| EEG | atividade elétrica no couro cabeludo | alta resolução temporal para questões de carga/atenção | artefatos oculares/musculares, montagem, ajuste dos eletrodos, textura/volume do cabelo e baixa aceitabilidade cotidiana | Pesquisa posterior e específica |
| Temperatura cutânea | termorregulação e resposta vasomotora periférica | contexto e complemento | ambiente, contato, ritmo circadiano, atividade e o próprio atuador confundem | Complementar |
| Respiração | ciclo e padrão respiratório | feature e controle de confusão para VFC | cinta e esforço de uso | Controlado ou amostra reduzida |
| IMU | movimento, não fisiologia | detecção de artefato e atividade | posição e comportamento do dispositivo | Essencial junto ao wearable |
| Ambiente | luz, som, temperatura, umidade, ar | contexto e alvo de adaptação | ocupantes, calibração, causalidade | Alto valor inicial |

Diretrizes recentes de HR/HRV distinguem laboratório e ambulatório e discutem diferenças entre ECG e PPG ([SPR committee report](https://pubmed.ncbi.nlm.nih.gov/38873876/)). Uma validação “lab-to-life” encontrou HR mais confiável do que HRV e menor disponibilidade de dados válidos no cotidiano; os números são específicos dos dispositivos testados, mas ilustram o problema de validade ecológica ([study](https://pubmed.ncbi.nlm.nih.gov/38528248/)).

Validação também precisa ser inclusiva e específica do hardware. A evidência sobre pigmentação e PPG é mista e varia por medida/dispositivo; portanto, não se deve presumir viés universal nem equivalência sem estratificar a avaliação por pigmentação medida objetivamente ([systematic review](https://pubmed.ncbi.nlm.nih.gov/39388258/)). Em EEG, volume e textura do cabelo podem alterar montagem, elegibilidade e qualidade, com risco de transformar diferença de aquisição em aparente diferença cognitiva ([study](https://pubmed.ncbi.nlm.nih.gov/38084752/)).

Temperatura cutânea por contato depende de pressão, fixação, equilíbrio térmico, sensor e ambiente; uma revisão metodológica específica deve orientar o protocolo ([contact thermometry review](https://pubmed.ncbi.nlm.nih.gov/29441024/)). A revisão de termografia citada nas fontes trata imagens infravermelhas de face/mãos em crianças de 0 a 12 anos; ela não valida automaticamente sensores de contato no punho nem adultos.

Para EDA, a nomenclatura e a necessidade de relatar local, eletrodos, processamento e artefatos são consolidadas pelas recomendações da Society for Psychophysiological Research ([EDA recommendations](https://doi.org/10.1111/j.1469-8986.2012.01384.x)).

### 8.3 Estado da arte relevante

As decisões de projeto abaixo são inferidas do conjunto de evidências sobre ambiguidade, contexto e perda de qualidade naturalística; nem todas foram testadas em conjunto por um único estudo:

- modelos dentro da pessoa, comparados à própria linha de base;
- EMA/autorrelato junto à fisiologia, não como detalhe posterior;
- IMU e ambiente para interpretar artefatos e contexto;
- fusão multimodal com capacidade de funcionar quando uma modalidade falta;
- classe explícita `unknown/insufficient_quality`;
- calibração e abstenção, não apenas acurácia;
- validação separada por participante, sessão, dia, ambiente e dispositivo;
- avaliação longitudinal fora do laboratório.

Uma pesquisa naturalística encontrou ativação associada tanto a estresse autorrelatado quanto a afeto positivo, e modelos individualizados com EMA mais fisiologia superaram abordagens isoladas ([EMA/EPA study](https://pmc.ncbi.nlm.nih.gov/articles/PMC10623231/)). Uma revisão de 104 estudos cotidianos mostrou associações concorrentes diretas em apenas parte das análises, com muitos moderadores e fatores de confusão ([systematic review](https://pubmed.ncbi.nlm.nih.gov/35895674/)). A fusão multimodal amplia evidência, mas não transforma um construto ambíguo em verdade objetiva; essa última frase é uma inferência de projeto apoiada pela ambiguidade descrita na literatura ([multimodal stress review](https://pubmed.ncbi.nlm.nih.gov/41829546/)).

### 8.4 Neurodivergência

A literatura revisada não sustenta um limiar universal validado para inferência individual. Em autismo, uma meta-análise específica de respostas autonômicas a estímulos sociais encontrou diferença geral pequena, não significativa e altamente heterogênea entre grupos autistas e neurotípicos ([meta-analysis](https://pubmed.ncbi.nlm.nih.gov/38073185/)). Em TDAH, padrões EEG estudados em grupo não justificam tradução diagnóstica direta ([systematic review](https://pubmed.ncbi.nlm.nih.gov/35760387/)). Essas duas fontes não representam toda a neurodivergência, todas as idades ou todas as modalidades.

Recomendações de projeto:

- modelar a pessoa, não um diagnóstico;
- tratar preferências e relato da pessoa como autoridade;
- usar fisiologia como evidência auxiliar;
- fazer co-design com participantes neurodivergentes;
- considerar desconforto sensorial, interocepção, alexitimia, sono, medicação, comorbidades e idade;
- adaptar gradualmente e permitir pausa, recusa, controle manual e desligamento;
- avaliar se a intervenção ajuda, não se apenas reduz um score fisiológico.

---

## 9. Arquitetura futura para biossinais

### 9.1 Pipeline recomendado

```text
sinais brutos + contexto
  -> sincronização e proveniência
  -> controle de qualidade/artefato
  -> features fisiológicas contextualizadas
  -> linha de base pessoal multissessão
  -> estimativa incerta com opção de abstenção
  -> confirmação/preferência quando necessário
  -> intent limitado e imutável
  -> arbitragem determinística no Solid
  -> ação ambiental gradual e reversível
  -> feedback da pessoa e avaliação do resultado
```

### 9.2 Separação de responsabilidades

**Fora do ECS Solid, em camada futura de aquisição:**

- streams brutos de ECG/EEG/EDA/PPG;
- buffers de alta frequência;
- sincronização temporal e resampling;
- arquivos de pesquisa e processamento offline;
- detalhes de firmware e SDK de dispositivo.

**Como snapshots/componentes limitados no futuro:**

- feature recente com janela e timestamp;
- qualidade e ausência de dados;
- proveniência do dispositivo e pipeline;
- contexto ambiental atual;
- preferência e capacidade de intervenção;
- estimativa com confiança/abstenção.

**Como intents:**

- proposta de reduzir luz gradualmente;
- proposta de reduzir som ou notificação;
- proposta de perguntar à pessoa;
- proposta de pausar uma automação.

Um intent não deve carregar uma alegação de verdade sobre a pessoa. Ele representa uma proposta limitada, com dono, alvo, lifetime, prioridade e origem auditável.

### 9.3 Cinco níveis de validação

1. **Validade do sensor:** comparação do sinal bruto com referência apropriada, por dispositivo e estratificada por fatores relevantes de aquisição, como pigmentação medida no PPG e ajuste/cabelo no EEG.
2. **Validade da feature:** HRV, SCR, temperatura ou EEG continuam válidos nas condições pretendidas?
3. **Validade do construto:** a feature/modelo se relaciona ao estado definido por EMA, escala, tarefa e contexto?
4. **Validade da intervenção:** a mudança ambiental realmente ajuda a pessoa?
5. **Usabilidade, equidade, acessibilidade e segurança:** o custo sensorial, a representatividade, os falsos disparos, a confiança e os controles são aceitáveis em diferentes participantes?

### 9.4 Requisitos mínimos para ML futuro

- definir primeiro o estimando: generalização populacional, personalização dentro da pessoa ou transferência híbrida;
- para generalização populacional, usar split por participante, como leave-one-subject-out, sem a mesma pessoa em treino e teste;
- para personalização, separar uma calibração inicial e testar cronologicamente em sessões/dias posteriores da mesma pessoa;
- formar grupos por pessoa, episódio e sessão antes de criar/dividir janelas, impedindo vazamento entre janelas sobrepostas;
- manter dispositivo/ambiente fora do treino quando a alegação incluir generalização para eles;
- comparar modelos pessoais, populacionais e híbridos;
- reportar ausência de dados, cobertura de qualidade, intervalo de confiança, calibração, abstenção e casos de falha;
- preservar rótulos incertos em vez de forçar uma emoção;
- versionar preprocessing, features, firmware, relógios, seeds e modelo;
- validar generalização antes de qualquer alegação ampla;
- nunca usar acurácia isolada como evidência de utilidade.

---

## 10. Privacidade, ética e fronteira regulatória

### 10.1 LGPD e risco de reidentificação

Como decisão de governança conservadora, o projeto deve proteger biossinais vinculados como se fossem dados sensíveis de saúde. A classificação jurídica concreta depende do conteúdo, finalidade e inferências: dados que revelem saúde são sensíveis; quando usados para identificação única, podem também ser biométricos. Perfis comportamentais identificáveis são dados pessoais. A definição legal está no [texto oficial da LGPD](https://www.planalto.gov.br/ccivil_03/_ato2015-2018/2018/lei/l13709compilado.htm).

Pseudonimização não equivale a anonimização. Uma revisão sistemática encontrou capacidade relevante de reidentificação a partir de sinais de wearables, inclusive segmentos curtos de ECG em alguns estudos ([review](https://pubmed.ncbi.nlm.nih.gov/36797124/)).

Antes de coleta real:

- fazer avaliação documentada de risco e produzir RIPD quando o tratamento puder gerar alto risco ou quando ele for solicitado/aplicável. O parâmetro atual da ANPD combina pelo menos um critério geral com um específico; dado sensível, sozinho, não torna todo tratamento automaticamente de alto risco ([orientação ANPD](https://www.gov.br/anpd/pt-br/canais_atendimento/agente-de-tratamento/relatorio-de-impacto-a-protecao-de-dados-pessoais-ripd));
- definir finalidade, base legal, controlador, operador e encarregado quando aplicável;
- inventariar dados, derivados, logs, compartilhamentos e retenção;
- minimizar coleta e processar localmente quando possível;
- separar chaves de identidade dos sinais;
- criptografar em trânsito e repouso;
- registrar acessos e aplicar privilégio mínimo;
- permitir pausa, retirada, acesso, exportação e eliminação quando aplicável;
- explicar inferências, incerteza e decisões automatizadas;
- considerar co-ocupantes e visitantes que não consentiram;
- evitar áudio/vídeo até haver necessidade documentada superior ao risco.

Pesquisa acadêmica não é automaticamente isenta. A ANPD esclarece bases e responsabilidades em seu [guia para fins acadêmicos e estudos](https://www.gov.br/anpd/pt-br/assuntos/noticias/anpd-lanca-guia-orientativo-sobre-tratamento-de-dados-pessoais-para-fins-academicos).

TCLE/consentimento ético e base legal da LGPD são análises relacionadas, mas não intercambiáveis. O tratamento de dados sensíveis precisa de hipótese aplicável do art. 11; revogação, eliminação, portabilidade e retenção dependem da base e do contexto. Se houver crianças ou adolescentes, aplicar o melhor interesse do art. 14, autorização do responsável quando cabível e assentimento acessível e contínuo da própria pessoa, sem presumir incapacidade por neurodivergência ([orientação ANPD](https://www.gov.br/anpd/pt-br/assuntos/noticias/anpd-divulga-enunciado-sobre-o-tratamento-de-dados-pessoais-de-criancas-e-adolescentes)).

### 10.2 Ética em pesquisa

O marco vigente mudou. A [Lei 14.874/2024](https://www.planalto.gov.br/ccivil_03/_ato2023-2026/2024/lei/l14874.htm) e o [Decreto 12.651/2025](https://www.planalto.gov.br/ccivil_03/_ato2023-2026/2025/decreto/d12651.htm) instituíram e regulamentaram o Sistema Nacional de Ética em Pesquisa com Seres Humanos (SINEP), com a Instância Nacional de Ética em Pesquisa (INAEP) e CEPs. A competência do CEP depende da classificação de risco: CEPs credenciados analisam risco baixo/moderado e CEPs acreditados podem analisar risco elevado, segundo as orientações de transição da [Nota Técnica 43/2025](https://www.gov.br/saude/pt-br/composicao/orgaos-colegiados/inaep/notas-tecnicas-e-informativas/nota-tecnica-no-43-2025-decit-sectics-ms) e da [Nota Técnica 1/2026](https://www.gov.br/saude/pt-br/centrais-de-conteudo/publicacoes/notas-tecnicas/2026/nota-tecnica-no-1-2026-decit-sctie-ms.pdf).

Antes de recrutar, observar ou registrar participantes, o protocolo deve seguir a rota indicada pela instituição e pelo SINEP/CEP. As Resoluções CNS 466/2012 e 510/2016 continuam relevantes naquilo que não conflitar com a Lei e o Decreto, conforme a transição descrita pela [INAEP](https://www.gov.br/saude/pt-br/composicao/orgaos-colegiados/inaep/inaep/); elas não devem ser apresentadas isoladamente como todo o marco atual.

O protocolo deve ainda prever:

- TCLE e assentimento em linguagem/formato acessível, confirmados ao longo do estudo;
- avaliação de carga sensorial, critérios claros de pausa/encerramento e retirada sem prejuízo;
- prevenção de coerção em relações com cuidadores, escola, empregador ou serviço de saúde;
- política para achados incidentais e limites do que o sistema pode comunicar;
- registro e tratamento de evento adverso, falso alerta e adaptação ambiental inadequada;
- plano de retirada de dados que explique o que pode ser eliminado e o que precisa ser retido;
- participação neurodivergente no desenho, sem equiparar neurodivergência a incapacidade.

A governança deve preservar autonomia, bem-estar, transparência, responsabilidade, inclusão, segurança e sustentabilidade, em linha com a [orientação da OMS para IA em saúde](https://www.who.int/publications/i/item/9789240029200).

### 10.3 Fronteira ANVISA

A finalidade pretendida e as alegações determinam se um software pode ser dispositivo médico, não apenas o algoritmo. A classificação de risco de SaMD segue a Regra 12 da RDC 751/2022; a RDC 657/2022 traz regras específicas/subsidiárias para software. A referência operacional atual é o [Manual para Regularização de Equipamento Médico e SaMD, versão 1.3, atualizado em 12/01/2026](https://www.gov.br/anvisa/pt-br/centraisdeconteudo/publicacoes/produtos-para-a-saude/manuais/manual-regularizacao-gquip/view). O FAQ de 2022 é histórico e não deve ser a única fonte.

Inferência de projeto, não decisão jurídica:

- apoio a conforto, acessibilidade, autorregulação e preferências controladas pela pessoa pode sustentar uma formulação não clínica, mas não existe isenção automática de “wellness”; o enquadramento é caso a caso;
- detectar condição, prever episódio clínico, monitorar fisiologia com finalidade médica, recomendar tratamento ou alertar profissionais pode cruzar a fronteira SaMD;
- documentação, UI, marketing, usuário pretendido e função real importam;
- um aviso ou disclaimer não neutraliza finalidade médica;
- deve existir uma etapa regulatória formal antes de alegações clínicas ou comercialização.

No estado atual, o repositório não coleta dados nem produz alegações clínicas. Portanto, essas são condições futuras, e não falhas de conformidade do código presente.

---

## 11. Contrato recomendado para M5

### 11.1 Forma mínima

M5 deve ser uma fronteira Lua explícita, não um scheduler completo:

```text
host selects live BehaviorId
  -> host creates restricted Lua state
  -> host exposes typed, named intent capabilities
  -> script proposes one or more intents
  -> host validates through World
  -> success commits; failure rolls back only newly-created intents
  -> result/error is recorded
```

### 11.2 Decisões que não precisam de nova escolha de produto

- O proprietário é fixado pelo host; Lua não recebe argumento de `BehaviorId`.
- Alvos são nomes previamente autorizados, não slots crus.
- Cada componente exposto possui função tipada de criação; não há reflection genérica sobre registries.
- `World`, `Coordinator`, `WorldState`, storages e pointers não são userdata Lua.
- Usar a API C oficial do Lua 5.4.8, fixada no build. Não adotar um binding C++ adicional sem um spike comparativo curto.
- Usar allowlist: `assert`, `error`, `ipairs`, `next`, `pairs`, `select`, `tonumber`, `type`, módulos sanitizados `table`, `string`, `math`, `utf8` e as C closures de proposta. `pcall` e `xpcall` ficam ausentes porque poderiam capturar repetidamente o erro do count hook; `tostring` também fica ausente para não revelar endereços de objetos.
- Nunca chamar `luaL_openlibs`. Não abrir `io`, `os`, `package`, `debug` ou `coroutine`; remover `dofile`, `loadfile`, `load`, `require`, `collectgarbage`, `getmetatable`, `setmetatable`, `rawget`, `rawset`, `string.dump`, `string.find`, `string.format`, `string.gmatch`, `string.gsub`, `string.match`, `math.random` e `math.randomseed`; não instalar searchers ou native loaders. As funções de pattern são removidas porque executam em C e escapam ao orçamento de instruções da VM. O manual oficial permite abrir bibliotecas individualmente ([Lua manual](https://www.lua.org/manual/5.4/manual.html#6)).
- Injetar tempo; não expor relógio de parede.
- Desabilitar aleatoriedade ou fornecer PRNG determinístico controlado pelo host.
- Usar protected call e converter erros em resultado, nunca em abort do frame.
- Aplicar orçamento de instruções no host com count hook e orçamento de memória com allocator customizado. O script não recebe controle sobre esses mecanismos.
- Em erro, remover apenas intents criadas naquela execução; intents anteriores permanecem imutáveis.
- Não permitir update/delete de intents existentes.
- Não permitir escrita direta em componentes.
- Uma execução não bloqueia indefinidamente o frame.

### 11.3 Testes mínimos de M5

Positivos:

- cria intent válido para proprietário fixado;
- preserva valor, lifetime, prioridade e alvo;
- participa da resolução existente;
- expira e é limpo pelas regras existentes;
- permite criar `UntilTime` já expirado e prova sua limpeza normal na resolução, preservando a semântica M2/M3;
- mantém ordem determinística entre execuções.

Negativos:

- proprietário inexistente ou falsificado;
- alvo não autorizado;
- acesso apenas leitura;
- slot removido/reutilizado;
- tipo/valor incompatível;
- kind ou formato de lifetime inválido;
- tentativa de obter/mutar intent existente;
- tentativa de obter registry/world/component pointer;
- presença/uso de `io`, `os`, `package`, `debug`, `coroutine`, `dofile`, `loadfile`, `load`, `require`, `collectgarbage`, metatable/raw access, `string.dump`, `math.random`, `math.randomseed` e native loading;
- loop infinito e estouro de memória;
- erro depois de criar uma ou mais intents;
- duas instâncias Lua tentando compartilhar estado;
- erro de script não impede outros sistemas nem corrompe o frame;
- todas as regressões M1-M4 em Debug e sanitizer.

---

## 12. Roteiro recomendado

### Etapa 0. Estabilização antes de M5

1. **Concluído:** fechar copy/move de `World`, `Runtime` e managers com storage compartilhado.
2. **Concluído:** reentrância durante execução e callbacks é rejeitada; transições de membership permanecem consistentes sob falha de callback.
3. **Concluído:** frames adotaram fail-stop e callbacks possuem política de commit explícita; o runner Lua isola erros e implementa rollback local das intents criadas pela execução.
4. **Concluído para o harness atual:** manter asserts ativos em Release; remover efeitos colaterais internos continua como limpeza futura.
5. **Concluído:** cobrir assinatura vazia e tie-break com reciclagem.
6. **Concluído:** declarar thread confinement e validade de pointers.
7. **Concluído para M4:** alinhar fronteira pública e ordem do frame nos documentos normativos.

### M5. Lua controlado

1. **Implementado:** contrato público e testes de comportamento.
2. **Implementado:** Lua 5.4.8 via API C oficial, fixado em CMake reprodutível.
3. **Implementado:** executor restrito e proprietário predefinido.
4. **Implementado:** capabilities tipadas de intent com snapshots frescos e cache revisionado.
5. **Implementado:** orçamentos, commit transacional, rollback e diagnóstico limitado.
6. **Implementado:** testes negativos de sandbox e regressões M1-M4.

### M6. Simulation CLI

1. Cenários reproduzíveis com tempo e seed explícitos.
2. Entrada de scripts e estado sem hardware.
3. Inspeção de frame, intents selecionadas e erros.
4. Golden scenarios para regressão.
5. Nenhum adapter físico ainda.

### Antes do Stage 2, Liquid

O tracking deve explicitar marcos para:

- aplicação de efeitos selecionados;
- entrada assíncrona por fila;
- serialization e replay;
- log durável e proveniência;
- política de segurança e controle manual;
- proposta adaptativa com incerteza e abstenção;
- confirmação humana e explicação.

### Stage 2, Liquid adaptativo

- inferência roda fora da thread proprietária;
- saída é proposta validada, não mutação;
- Solid continua sendo a autoridade de permissão e arbitragem;
- toda proposta registra modelo, versão, features, confiança e contexto;
- baixa confiança produz abstenção ou pergunta;
- simulação e replay precedem atuação física.

### Stage 3, Liquid Layer

Sequência recomendada:

1. co-design e mapeamento de necessidades/preferências;
2. sensing ambiental e controles manuais;
3. regras determinísticas com controle manual;
4. piloto de viabilidade com o menor subconjunto de sensores justificado pela pergunta; PPG, EDA, temperatura cutânea e IMU são candidatos, enquanto ambiente e EMA fornecem contexto e referência;
5. validação contra referências apropriadas;
6. modelo dentro da pessoa com incerteza;
7. estudo controlado de intervenção;
8. estudo longitudinal cotidiano;
9. JITAI apenas depois das validações anteriores;
10. EEG somente para pergunta específica que sensores menos invasivos não respondem.

---

## 13. Registro de riscos

| Risco | Probabilidade atual | Impacto | Tratamento recomendado |
|---|---|---|---|
| Reintrodução de copy/move sem semântica de clone | Baixa | Alto | Operações apagadas e `static_assert`; exigir design explícito antes de reabrir |
| Mutação estrutural durante `run`/callback | Baixa | Alto | Rejeição imediata, topologia congelada e referência forte local |
| Callback de membership lançar | Baixa | Médio | Callback observacional/no-throw; transição completa e primeiro erro relançado |
| Exceção deixar frame parcial | Baixa | Alto | Runtime fail-stop; runner Lua contém erros e reverte apenas suas novas intents |
| Regressão que volte a desativar asserts em Release | Baixa | Alto | Opção de compilação por alvo e gate Release 12/12 |
| Lua escapar da capability API | Baixa na fronteira atual | Alto | Estado fresco restrito, closures host-bound, bindings enumerados e regressões adversariais |
| Thread de sensor mutar World | Alta em etapas futuras | Alto | Owner thread e fila de propostas |
| Inferir emoção/diagnóstico de correlato | Alta sem governança | Alto | Claims conservadores e validação em camadas |
| Dataset pequeno/vazamento entre janelas | Alta em TCC | Alto | Split por pessoa/sessão e protocolo prévio |
| Wearable perder qualidade no cotidiano | Alta | Médio/alto | Controle de qualidade, IMU e abstenção |
| Sensor funcionar pior por pigmentação, cabelo ou ajuste | Dependente do dispositivo | Alto | Validação estratificada, hardware inclusivo e transparência |
| Modelo não generalizar entre pessoas | Alta | Alto | Personalização e avaliação longitudinal |
| Reidentificação de biossinais | Média/alta | Alto | Minimização, separação, acesso e RIPD |
| Adaptação reduzir autonomia | Média | Alto | Co-design, visibilidade, pausa e controle manual |
| Alegação cruzar fronteira SaMD | Dependente da finalidade | Alto | Etapa regulatória antes da alegação |

---

## 14. Decisões consolidadas desta avaliação

Estas decisões são coerentes com o código e não exigem escolha de produto adicional neste momento:

1. `AGENTS.md` governa escopo; tracking governa status.
2. M5 é limitado à fronteira; agendamento por componente pode ficar para depois.
3. Lua recebe capacidades, nunca o mundo.
4. Owner é escolhido pelo host e não pode ser falsificado pelo script.
5. Execução Lua deve ser limitada, determinística, protegida e atômica em relação às intents que ela mesma cria.
6. `World`, `Runtime`, `Coordinator` e managers stateful não são copiáveis nem movíveis até existir semântica correta.
7. O core é single-thread; async retorna propostas por fila.
8. O tie-break de M5 continua sendo maior `IntentId`, não recência, até decisão explícita posterior.
9. Streams fisiológicos brutos não pertencem ao ECS nem ao frame log.
10. O primeiro valor de Stage 3 vem de preferências e contexto ambiental; EEG não é sensor inicial.
11. Fisiologia é evidência pessoal e incerta, não diagnóstico ou autoridade sobre o usuário.
12. Nenhuma coleta humana começa antes de protocolo, CEP, plano de dados, avaliação LGPD documentada e RIPD quando o tratamento for de alto risco ou aplicável.

---

## 15. Avaliação final do trabalho realizado

### O que foi feito especialmente bem

- A separação Solid/Liquid/Liquid Layer é intelectualmente clara.
- O projeto evita introduzir LLM, hardware e adapters antes do núcleo.
- Intents imutáveis são uma boa unidade de proposta e auditoria.
- Permissões por comportamento, tipo e slot são coerentes com componentes compartilhados.
- O tempo explícito e a ordem determinística favorecem teste e replay.
- M1-M4 cresceram em passos pequenos, com testes de estresse e sanitizers opcionais.
- A documentação operacional registra o que não construir, algo raro e valioso.

### O que ainda falta após a fronteira M5

- manifesto machine-readable de schemas para futura geração de scripts por modelo;
- integração LLM e política limitada de repair/retry, fora do escopo de M5;
- aplicação futura de efeito, replay e observabilidade durável.

### Veredito

**M5 está concluído e M6, Simulation CLI, é o marco corrente.** Ownership, autoridade de fases, membership, callbacks, tempo monotônico, intenção no mesmo frame, falha de frame, garantias locais de exceção, sandbox Lua e matrizes de teste possuem contratos verificáveis. O próximo trabalho é disponibilizar esse núcleo por uma CLI determinística e sem hardware, sem introduzir uma segunda arquitetura de execução.

**A visão de futuro é promissora e combina bem com a arquitetura atual.** O ponto forte será usar um núcleo determinístico para limitar inferências incertas. O ponto de maior risco será tratar correlação fisiológica como conhecimento sobre a pessoa. A melhor versão do Liquid Layer não “sabe como alguém se sente”; ela percebe mudanças contextuais com incerteza, respeita preferências e oferece apoio reversível sem retirar autonomia.

---

## 16. Fontes externas selecionadas

### Fundamentos e medição

- Fairclough SH. “Fundamentals of physiological computing.” *Interacting with Computers*, 2009;21(1-2). [DOI 10.1016/j.intcom.2008.10.011](https://doi.org/10.1016/j.intcom.2008.10.011).
- SPR Ad Hoc Committee on Electrodermal Measures. “Publication recommendations for electrodermal measurements.” *Psychophysiology*, 2012;49(8):1017-1034. [DOI 10.1111/j.1469-8986.2012.01384.x](https://doi.org/10.1111/j.1469-8986.2012.01384.x).
- Society for Psychophysiological Research Committee. “Publication guidelines for human heart rate and heart rate variability studies in psychophysiology, Part 1.” *Psychophysiology*, 2024;61(9):e14604. [PMID 38873876](https://pubmed.ncbi.nlm.nih.gov/38873876/).
- “From lab to life: Evaluating the reliability and validity of psychophysiological data from wearable devices in laboratory and ambulatory settings.” 2024. [PMID 38528248](https://pubmed.ncbi.nlm.nih.gov/38528248/).
- Anders C, Arnrich B. “Wearable electroencephalography and multi-modal mental state classification: A systematic literature review.” *Computers in Biology and Medicine*, 2022;150:106088. [PMID 36137314](https://pubmed.ncbi.nlm.nih.gov/36137314/).
- “Infrared Thermal Imaging (ITI), a Non-invasive Window Into Early Emotion Regulation: A Systematic Review.” 2025; termografia de face/mãos em crianças de 0-12 anos. [PMID 40791016](https://pubmed.ncbi.nlm.nih.gov/40791016/).
- “Skin Temperature Measurement Using Contact Thermometry: A Systematic Review of Setup Variables and Their Effects on Measured Values.” 2018. [PMID 29441024](https://pubmed.ncbi.nlm.nih.gov/29441024/).
- Singh S et al. “Impact of Skin Pigmentation on Pulse Oximetry Blood Oxygenation and Wearable Pulse Rate Accuracy: Systematic Review and Meta-Analysis.” *JMIR*, 2024. [PMID 39388258](https://pubmed.ncbi.nlm.nih.gov/39388258/).
- Lees T et al. “The effect of hair type and texture on electroencephalography and event-related potential data quality.” *Psychophysiology*, 2024;61(3):e14499. [PMID 38084752](https://pubmed.ncbi.nlm.nih.gov/38084752/).

### Evidência, contexto e generalização

- Quigley KS, Barrett LF. “Is there consistency and specificity of autonomic changes during emotional episodes?” *Biological Psychology*, 2014;98:82-94. [PMID 24388802](https://pubmed.ncbi.nlm.nih.gov/24388802/).
- Charles RL, Nixon J. “Measuring mental workload using physiological measures: A systematic review.” *Applied Ergonomics*, 2019;74:221-232. [PMID 30487103](https://pubmed.ncbi.nlm.nih.gov/30487103/).
- “Detecting Prolonged Stress in Real Life Using Wearable Biosensors and Ecological Momentary Assessments: Naturalistic Experimental Study.” 2023. [PMCID PMC10623231](https://pmc.ncbi.nlm.nih.gov/articles/PMC10623231/).
- Weber J, Angerer P, Apolinário-Hagen J. “Physiological reactions to acute stressors and subjective stress during daily life: A systematic review on EMA studies.” *PLOS ONE*, 2022;17(7):e0271996. [PMID 35895674](https://pubmed.ncbi.nlm.nih.gov/35895674/).
- “Beyond EDA: A Systematic Review of Multimodal Sympathetic Nervous System Arousal Classification for Stress Detection.” 2026. [PMID 41829546](https://pubmed.ncbi.nlm.nih.gov/41829546/).
- Vos G, Trinh K, Sarnyai Z, Rahimi Azghadi M. “Generalizable machine learning for stress monitoring from wearable devices.” *International Journal of Medical Informatics*, 2023;173:105026. [PMID 36893657](https://pubmed.ncbi.nlm.nih.gov/36893657/).

### Neurodivergência

- Zadok E et al. “Autonomic nervous system responses to social stimuli among autistic individuals: A systematic review and meta-analysis.” *Autism Research*, 2024;17(3):497-511. [PMID 38073185](https://pubmed.ncbi.nlm.nih.gov/38073185/).
- Slater J et al. “Can electroencephalography (EEG) identify ADHD subtypes? A systematic review.” *Neuroscience & Biobehavioral Reviews*, 2022;139:104752. [PMID 35760387](https://pubmed.ncbi.nlm.nih.gov/35760387/).
- “Commercial Wearables for the Management of People with Autism Spectrum Disorder: A Review.” 2024. [PMCID PMC11591563](https://pmc.ncbi.nlm.nih.gov/articles/PMC11591563/).

### Privacidade, ética e regulação

- Brasil. Lei 13.709/2018, Lei Geral de Proteção de Dados, texto compilado. [Planalto](https://www.planalto.gov.br/ccivil_03/_ato2015-2018/2018/lei/l13709compilado.htm), acesso em 10 jul. 2026.
- ANPD. “Tratamento de dados pessoais para fins acadêmicos e para estudos e pesquisas.” Guia orientativo. [ANPD](https://www.gov.br/anpd/pt-br/assuntos/noticias/anpd-lanca-guia-orientativo-sobre-tratamento-de-dados-pessoais-para-fins-academicos), acesso em 10 jul. 2026.
- ANPD. “Relatório de Impacto à Proteção de Dados Pessoais (RIPD).” Inclui teste atual de alto risco. [ANPD](https://www.gov.br/anpd/pt-br/canais_atendimento/agente-de-tratamento/relatorio-de-impacto-a-protecao-de-dados-pessoais-ripd), acesso em 10 jul. 2026.
- “Does de-identification of data from wearables give us a false sense of security? A systematic review.” 2023. [PMID 36797124](https://pubmed.ncbi.nlm.nih.gov/36797124/).
- Brasil. Lei 14.874/2024, pesquisa com seres humanos e SINEP. [Planalto](https://www.planalto.gov.br/ccivil_03/_ato2023-2026/2024/lei/l14874.htm), acesso em 10 jul. 2026.
- Brasil. Decreto 12.651/2025, regulamentação da Lei 14.874/2024. [Planalto](https://www.planalto.gov.br/ccivil_03/_ato2023-2026/2025/decreto/d12651.htm), acesso em 10 jul. 2026.
- Ministério da Saúde/INAEP. Nota Técnica 43/2025, classificação de risco e tramitação no SINEP. [INAEP](https://www.gov.br/saude/pt-br/composicao/orgaos-colegiados/inaep/notas-tecnicas-e-informativas/nota-tecnica-no-43-2025-decit-sectics-ms), acesso em 10 jul. 2026.
- Ministério da Saúde/INAEP. Nota Técnica 1/2026, orientações de transição do SINEP. [PDF oficial](https://www.gov.br/saude/pt-br/centrais-de-conteudo/publicacoes/notas-tecnicas/2026/nota-tecnica-no-1-2026-decit-sctie-ms.pdf), acesso em 10 jul. 2026.
- CNS. Resoluções 466/2012 e 510/2016, aplicáveis em conjunto com o marco posterior naquilo que não houver conflito: [466](https://www.gov.br/conselho-nacional-de-saude/pt-br/acesso-a-informacao/atos-normativos/resolucoes/2012/resolucao-no-466.pdf), [510](https://www.gov.br/conselho-nacional-de-saude/pt-br/acesso-a-informacao/atos-normativos/resolucoes/2016/resolucao-no-510.pdf/view).
- WHO. “Ethics and governance of artificial intelligence for health.” 2021. [WHO](https://www.who.int/publications/i/item/9789240029200).
- ANVISA. “Manual para Regularização de Equipamento Médico e Software como Dispositivo Médico”, versão 1.3, atualizado em 12 jan. 2026. [ANVISA](https://www.gov.br/anvisa/pt-br/centraisdeconteudo/publicacoes/produtos-para-a-saude/manuais/manual-regularizacao-gquip/view), acesso em 10 jul. 2026.
- ANVISA. RDC 751/2022, classificação pela finalidade pretendida e Regra 12 para SaMD. [Página de classificação](https://www.gov.br/anvisa/pt-br/setorregulado/regularizacao/produtos-para-saude/conceitos-e-definicoes/classificacao-de-equipamentos), acesso em 10 jul. 2026.

### Lua

- Manual oficial Lua e bibliotecas selecionáveis: [Lua 5.4 manual](https://www.lua.org/manual/5.4/).
- Histórico oficial de versões: [Lua versions](https://www.lua.org/versions.html).
