# NNUE Training Hyperparameter Schedule (UHO 2024 8mv +0.85/+0.94)

This plan refines the final phase of the NNUE training run used for the UHO 2024 8-move suite (+0.85/+0.94). The focus is to
extract sharper tactical play from 2.71-dev-220925-thsaf while limiting overfitting to the white side of the starting book.

## Learning-rate schedule

| Phase | Portion of total steps | Learning rate | Notes |
|-------|------------------------|---------------|-------|
| Warm-up | 0.00 – 0.10 | 7.5e-4 | Fast burn-in to stabilise AdamW statistics. |
| Mid training | 0.10 – 0.65 | 5.0e-4 | Main chunk of training with constant rate. |
| Tactical focus | 0.65 – 0.85 | 2.5e-4 | Reduces step size before fine-tuning. |
| Fine-tuning | 0.85 – 0.95 | 1.5e-4 | New lower rate to capture delicate tactical corrections. |
| Polishing | 0.95 – 1.00 | 7.5e-5 | Final annealing pass requested for sharper move ordering. |

*The two last phases replace the former flat 2.5e-4 ending, implementing the requested reduction of the learning rate during the
final segment of training.*

When generating run scripts, configure the optimiser to respect the table above. With `torch.optim.lr_scheduler.SequentialLR`,
this translates to milestones at 10%, 65%, 85% and 95% of the total number of optimisation steps.

## Regularisation update

Increase the weight decay applied by the AdamW optimiser from **1.0e-5** to **1.6e-5**. This is a 60 % bump that adds enough
pressure to prevent the network from overspecialising in the favourable white positions of the UHO suite without freezing the
weights entirely.

When launching training:

```python
optimizer = torch.optim.AdamW(
    model.parameters(),
    lr=7.5e-4,
    betas=(0.9, 0.95),
    weight_decay=1.6e-5,
)
```

Keep dropout and other augmentations unchanged; empirical runs show that the heavier decay already reduces evaluation drift.

## Dataset handling

* Refresh both self-play and external reference games so that each colour sees the +0.85/+0.94 book equally often.
* Preserve the 50/50 colour balance when mirroring the starting positions; training batches must mix both perspectives.
* Continue injecting ~8 % of tactical blunders from the 2.40 baseline to ensure the network learns to punish exploitable ideas.

## Verification checklist

1. Generate a dry-run curve (`tensorboard --logdir runs/latest`) to confirm the stepped drop in the learning-rate plot.
2. Compare validation accuracy on the held-out tactical suite; expect at least a 0.3 % gain relative to the previous baseline.
3. Run a 200-game fast match (10+0.1) against 2.40 to verify that the new model no longer collapses in the Stonewall/Leningrado
   defences highlighted in the recent regression report.
4. If metrics deteriorate, revert only the final two learning-rate stages and retest before touching weight decay.

These steps put the infrastructure in place to relaunch the SPRT with the requested late-phase fine-tuning and stronger
regularisation against white-sided overfitting.
